#include "AirtableClient.hh"

#include <inttypes.h>
#include <stdio.h>

#include <format>
#include <phosg/Network.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

#include "AsyncUtils.hh"

using namespace std;

AirtableClient::ListRecordsOptions::ListRecordsOptions()
    : max_records(0),
      page_size(100) {}

AirtableClient::AirtableClient(
    asio::io_context& io_context,
    const string& access_token,
    const string& api_domain,
    uint16_t api_port)
    : AsyncHTTPClient(io_context),
      access_token(access_token),
      hostname(api_domain),
      port(api_port) {}

asio::awaitable<phosg::JSON> AirtableClient::make_api_call(
    HTTPRequest::Method method,
    string&& path,
    unordered_multimap<string, string>&& query_params,
    const phosg::JSON* json,
    bool parse_response) {

  // TODO: Make try count configurable
  for (size_t try_num = 0; try_num < 3; try_num++) {
    HTTPRequest req;
    req.method = method;
    req.https = true;
    req.domain = this->hostname;
    req.port = this->port;
    req.path = std::move(path);
    req.query_params = std::move(query_params);
    req.http_version = "HTTP/1.1";
    req.headers.emplace("Host", this->hostname);
    req.headers.emplace("Authorization", "Bearer " + this->access_token);
    req.headers.emplace("Connection", "close"); // TODO: Support keep-alive
    if (json) {
      req.headers.emplace("Content-Type", "application/json");
      req.data = json->serialize();
    }

    auto resp = co_await this->make_request(req);

    if ((resp.response_code >= 500) && (resp.response_code <= 599)) {
      // 0 means some non-HTTP error occurred, like connect() failed or SSL
      // negotiation failed. 5xx means a server error occurred. In either case,
      // we'll retry again immediately.
      throw runtime_error(std::format("API returned HTTP {}", resp.response_code));

    } else if (resp.response_code == 429) {
      // Rate-limited. We should wait at least 30 seconds before trying again.
      // If this is the final try, don't wait and just throw the 429 to the
      // caller like all other client (4xx) error codes.
      // TODO: We probably should make this configurable; callers may not want
      // to wait this long.
      co_await async_sleep(std::chrono::seconds(30));
      continue;

    } else if (resp.response_code != 200) {
      throw runtime_error(std::format("API returned HTTP {}", resp.response_code));
    }

    if (parse_response) {
      co_return phosg::JSON::parse(resp.data);
    } else {
      co_return nullptr; // Becomes JSON null
    }
  }
  throw runtime_error("Failed to make API call after 3 tries");
}

asio::awaitable<vector<BaseInfo>> AirtableClient::list_bases() {
  auto response_json = co_await this->make_api_call(HTTPRequest::Method::GET, "/v0/meta/bases");

  vector<BaseInfo> ret;
  for (const auto& base_json : response_json.at("bases").as_list()) {
    BaseInfo& base = ret.emplace_back();
    base.base_id = base_json->at("id").as_string();
    base.name = base_json->at("name").as_string();
    base.permission_level = base_json->at("permissionLevel").as_string();
  }

  co_return ret;
}

asio::awaitable<unordered_map<string, TableSchema>> AirtableClient::get_base_schema(const string& base_id) {
  auto response_json = co_await this->make_api_call(HTTPRequest::Method::GET, "/v0/meta/bases/" + base_id + "/tables");

  unordered_map<string, TableSchema> ret;
  for (const auto& table_json : response_json.at("tables").as_list()) {
    TableSchema& table = ret[table_json->at("id").as_string()];
    table.name = table_json->at("name").as_string();
    table.primary_field_id = table_json->at("primaryFieldId").as_string();
    for (const auto& field_json : table_json->at("fields").as_list()) {
      TableSchema::FieldSchema& field = table.fields[field_json->at("id").as_string()];
      field.name = field_json->at("name").as_string();
      field.type = field_json->at("type").as_string();
      try {
        field.options = field_json->at("options");
      } catch (const std::out_of_range&) {
      }
    }
    for (const auto& view_json : table_json->at("views").as_list()) {
      TableSchema::ViewSchema& view = table.views[view_json->at("id").as_string()];
      view.name = view_json->at("name").as_string();
      view.type = view_json->at("type").as_string();
    }
  }

  co_return ret;
}

asio::awaitable<pair<vector<Record>, string>> AirtableClient::list_records_page(
    const string& base_id,
    const string& table_name,
    const ListRecordsOptions* options,
    const string& offset) {
  if (!options) {
    static const ListRecordsOptions default_options;
    options = &default_options;
  }

  unordered_multimap<string, string> query_params;
  for (const auto& field : options->fields) {
    query_params.emplace("fields[]", field);
  }
  if (!options->filter_formula.empty()) {
    query_params.emplace("filterByFormula", options->filter_formula);
  }
  if (options->max_records) {
    query_params.emplace("maxRecords", std::format("{}", options->max_records));
  }
  if (options->page_size) {
    query_params.emplace("pageSize", std::format("{}", options->page_size));
  }
  for (size_t x = 0; x < options->sort_fields.size(); x++) {
    const auto& sort = options->sort_fields[x];
    query_params.emplace(std::format("sort[{}][field]", x), sort.first);
    query_params.emplace(std::format("sort[{}][direction]", x), sort.second ? "asc" : "desc");
  }
  if (!options->view.empty()) {
    query_params.emplace("view", options->view);
  }
  if (!offset.empty()) {
    query_params.emplace("offset", offset);
  }

  auto response_json = co_await this->make_api_call(HTTPRequest::Method::GET, "/v0/" + base_id + "/" + table_name, std::move(query_params));

  const auto& record_jsons = response_json.at("records").as_list();
  vector<Record> ret;
  for (const auto& record_json : record_jsons) {
    ret.emplace_back(*record_json);
  }

  string next_offset;
  try {
    next_offset = response_json.at("offset").as_string();
  } catch (const out_of_range&) {
  }

  co_return make_pair(std::move(ret), std::move(next_offset));
}

asio::awaitable<vector<Record>> AirtableClient::list_records(
    const string& base_id, const string& table_name, const ListRecordsOptions* options) {
  vector<Record> ret;
  string offset;
  do {
    auto page_ret = co_await this->list_records_page(base_id, table_name, options, offset);
    ret.insert(ret.end(), make_move_iterator(page_ret.first.begin()), make_move_iterator(page_ret.first.end()));
    offset = std::move(page_ret.second);
  } while (!offset.empty());

  co_return ret;
};

asio::awaitable<Record> AirtableClient::get_record(const string& base_id, const string& table_name, const string& record_id) {
  auto response_json = co_await this->make_api_call(HTTPRequest::Method::GET, "/v0/" + base_id + "/" + table_name + "/" + record_id);
  co_return Record(response_json);
}

asio::awaitable<vector<string>> AirtableClient::create_records(
    const string& base_id,
    const string& table_name,
    const vector<unordered_map<string, shared_ptr<Field>>>& contents,
    bool parse_response) {
  auto record_jsons = phosg::JSON::list();
  for (const auto& it : contents) {
    record_jsons.emplace_back(Record::json_for_create(it));
  }
  auto root_json = phosg::JSON::dict({{"records", std::move(record_jsons)}});

  auto response_json = co_await this->make_api_call(HTTPRequest::Method::POST, "/v0/" + base_id + "/" + table_name, {}, &root_json, parse_response);

  vector<string> ret;
  if (parse_response) {
    const auto& records = response_json.at("records").as_list();
    for (const auto& record_json : records) {
      ret.emplace_back(record_json->at("id").as_string());
    }
  }
  co_return ret;
}

asio::awaitable<vector<Record>> AirtableClient::update_records(
    const string& base_id,
    const string& table_name,
    const unordered_map<string, unordered_map<string, shared_ptr<Field>>>& contents,
    bool parse_response) {

  auto records = phosg::JSON::list();
  for (const auto& it : contents) {
    auto json = Record::json_for_create(it.second);
    json.emplace("id", it.first);
    records.emplace_back(std::move(json));
  }
  auto root_json = phosg::JSON::dict({{"records", std::move(records)}});

  auto response_json = co_await this->make_api_call(HTTPRequest::Method::PATCH, "/v0/" + base_id + "/" + table_name, {}, &root_json, parse_response);

  vector<Record> ret;
  if (parse_response) {
    const auto& records = response_json.at("records").as_list();
    for (const auto& record_json : records) {
      ret.emplace_back(*record_json);
    }
  }
  co_return ret;
}

asio::awaitable<unordered_map<string, bool>> AirtableClient::delete_records(
    const string& base_id, const string& table_name, const vector<string>& record_ids, bool parse_response) {

  std::unordered_multimap<string, string> query_params;
  for (const auto& record_id : record_ids) {
    query_params.emplace("records[]", record_id);
  }

  auto response_json = co_await this->make_api_call(
      HTTPRequest::Method::DELETE, "/v0/" + base_id + "/" + table_name, std::move(query_params), nullptr, parse_response);

  unordered_map<string, bool> ret;
  if (parse_response) {
    const auto& records = response_json.at("records").as_list();
    for (const auto& record_json : records) {
      ret.emplace(record_json->at("id").as_string(), record_json->at("deleted").as_bool());
    }
  }
  co_return ret;
}
