#include "AirtableClient.hh"

#include <inttypes.h>
#include <stdio.h>

#include <phosg/Strings.hh>
#include <phosg/Network.hh>
#include <stdexcept>

using namespace std;



AirtableClient::ListRecordsOptions::ListRecordsOptions()
  : max_records(0), page_size(100) { }



AirtableClient::AirtableClient(
    EventAsync::Base& base,
    EventAsync::DNSBase& dns_base,
    const string& api_key,
    const string& client_secret,
    SSL_CTX* ssl_ctx,
    const string& api_domain) :
    base(base),
    dns_base(dns_base),
    api_key(api_key),
    client_secret(client_secret),
    ssl_ctx(ssl_ctx),
    // TODO: make these configurable
    max_retries(3),
    request_timeout(10) {

  if (!this->ssl_ctx) {
    this->owned_ssl_ctx = true;
    this->ssl_ctx = EventAsync::HTTP::Connection::create_default_ssl_ctx();
  } else {
    this->owned_ssl_ctx = false;
  }

  auto netloc_parsed = parse_netloc(api_domain, 443);
  this->hostname = netloc_parsed.first;
  this->port = netloc_parsed.second;
}

AirtableClient::~AirtableClient() {
  if (this->owned_ssl_ctx) {
    SSL_CTX_free(this->ssl_ctx);
  }
}



EventAsync::Task<shared_ptr<JSONObject>> AirtableClient::make_api_call(
    evhttp_cmd_type method,
    const string& path,
    const string& query,
    shared_ptr<const JSONObject> json,
    bool parse_response) {

  for (size_t x = 0; x < this->max_retries; x++) {
    bool is_final_try = (x == this->max_retries - 1);

    EventAsync::HTTP::Connection conn(
        this->base,
        this->dns_base,
        this->hostname.c_str(),
        this->port,
        this->ssl_ctx);
    conn.set_timeout(this->request_timeout);

    EventAsync::HTTP::Request req(this->base);

    string auth_header = "Bearer " + this->api_key;
    req.add_output_header("Host", this->hostname.c_str());
    req.add_output_header("Connection", "close");
    req.add_output_header("Authorization", auth_header.c_str());
    if (!this->client_secret.empty()) {
      req.add_output_header(
          "X-Airtable-Client-Secret", this->client_secret.c_str());
    }

    string serialized_post_data;
    if (json.get()) {
      serialized_post_data = json->serialize();

      req.get_output_buffer().add_reference(
          serialized_post_data.data(), serialized_post_data.size());

      string content_length = string_printf("%zu", serialized_post_data.size());
      req.add_output_header("Content-Type", "application/json");
      req.add_output_header("Content-Length", content_length.c_str());
    }

    string request_path = escape_url(path, false);
    if (!query.empty()) {
      request_path += '?';
      request_path += escape_url(query, true);
    }

    co_await conn.send_request(req, method, request_path.c_str());

    int response_code = req.get_response_code();
    if ((response_code == 0) ||
        ((response_code >= 500) && (response_code <= 599))) {
      // 0 means some non-HTTP error occurred, like connect() failed or SSL
      // negotiation failed. 5xx means a server error occurred. In either case,
      // we'll retry again immediately.
      if (is_final_try) {
        throw runtime_error(response_code == 0
            ? "http request did not complete"
            : string_printf("api returned http %d", response_code));
      }
      continue;

    } else if (response_code == 429 && !is_final_try) {
      // Rate-limited. We should wait at least 30 seconds before trying again.
      // If this is the final try, don't wait and just throw the 429 to the
      // caller like all other client (4xx) error codes.
      // TODO: We probably should make this configurable; callers may not want
      // to wait this long.
      co_await base.sleep(30000000);
      continue;

    } else if (response_code != 200) {
      throw runtime_error(string_printf("api returned http %d", response_code));
    }

    if (parse_response) {
      // TODO: it would be nice to do this without linearizing the input buffer
      auto in_buf = req.get_input_buffer();
      string in_data = in_buf.remove(in_buf.get_length());
      co_return JSONObject::parse(in_data);
    } else {
      co_return nullptr;
    }
  }

  throw logic_error("request loop terminated without throwing or returning");
}



EventAsync::Task<vector<BaseInfo>> AirtableClient::list_bases() {
  if (this->client_secret.empty()) {
    throw runtime_error("a client secret is required for list_bases");
  }

  shared_ptr<JSONObject> response_json = co_await this->make_api_call(
      EVHTTP_REQ_GET, "/v0/meta/bases");

  auto root = response_json->as_dict();
  vector<BaseInfo> ret;
  for (const auto& base_json : root.at("bases")->as_list()) {
    BaseInfo& base = ret.emplace_back();
    base.base_id = base_json->at("id")->as_string();
    base.name = base_json->at("name")->as_string();
    base.permission_level = base_json->at("permissionLevel")->as_string();
  }

  co_return ret;
}

EventAsync::Task<unordered_map<string, TableSchema>>
AirtableClient::get_base_schema(const string& base_id) {
  if (this->client_secret.empty()) {
    throw runtime_error("a client secret is requirefd for get_base_schema");
  }

  shared_ptr<JSONObject> response_json = co_await this->make_api_call(
      EVHTTP_REQ_GET, "/v0/meta/bases/" + base_id + "/tables");

  auto root = response_json->as_dict();

  unordered_map<string, TableSchema> ret;
  for (const auto& table_json : root.at("tables")->as_list()) {
    TableSchema& table = ret[table_json->at("id")->as_string()];
    table.name = table_json->at("name")->as_string();
    table.primary_field_id = table_json->at("primaryFieldId")->as_string();
    for (const auto& field_json : table_json->at("fields")->as_list()) {
      TableSchema::FieldSchema& field = table.fields[field_json->at("id")->as_string()];
      field.name = field_json->at("name")->as_string();
      field.type = field_json->at("type")->as_string();
      try {
        field.options = field_json->at("options");
      } catch (const JSONObject::key_error&) { }
    }
    for (const auto& view_json : table_json->at("views")->as_list()) {
      TableSchema::ViewSchema& view = table.views[view_json->at("id")->as_string()];
      view.name = view_json->at("name")->as_string();
      view.type = view_json->at("type")->as_string();
    }
  }

  co_return ret;
}



EventAsync::Task<pair<vector<Record>, string>> AirtableClient::list_records_page(
    const string& base_id,
    const string& table_name,
    const ListRecordsOptions* options,
    const string& offset) {
  if (!options) {
    static const ListRecordsOptions default_options;
    options = &default_options;
  }

  vector<string> query_params;
  for (const auto& field : options->fields) {
    query_params.emplace_back("fields[]=" + field);
  }
  if (!options->filter_formula.empty()) {
    query_params.emplace_back("filterByFormula=" + options->filter_formula);
  }
  if (options->max_records) {
    query_params.emplace_back(
        string_printf("maxRecords=%zu", options->max_records));
  }
  if (options->page_size) {
    query_params.emplace_back(
        string_printf("pageSize=%zu", options->page_size));
  }
  for (size_t x = 0; x < options->sort_fields.size(); x++) {
    const auto& sort = options->sort_fields[x];
    query_params.emplace_back(
        string_printf("sort[%zu][field]=%s", x, sort.first.c_str()));
    query_params.emplace_back(
        string_printf("sort[%zu][direction]=%s", x,
          sort.second ? "asc" : "desc"));
  }
  if (!options->view.empty()) {
    query_params.emplace_back("view=" + options->view);
  }
  if (!offset.empty()) {
    query_params.emplace_back("offset=" + offset);
  }

  string query_string = join(query_params, "&");

  shared_ptr<JSONObject> response_json = co_await this->make_api_call(
      EVHTTP_REQ_GET, "/v0/" + base_id + "/" + table_name, query_string);

  auto response_dict = response_json->as_dict();
  const auto& record_jsons = response_dict.at("records")->as_list();
  vector<Record> ret;
  for (const auto& record_json : record_jsons) {
    ret.emplace_back(record_json);
  }

  string next_offset;
  try {
    next_offset = response_dict.at("offset")->as_string();
  } catch (const out_of_range&) { }

  co_return make_pair(move(ret), move(next_offset));
}

EventAsync::Task<vector<Record>> AirtableClient::list_records(
    const string& base_id,
    const string& table_name,
    const ListRecordsOptions* options) {
  vector<Record> ret;
  string offset;
  do {
    auto page_ret = co_await this->list_records_page(
        base_id, table_name, options, offset);
    ret.insert(
        ret.end(),
        make_move_iterator(page_ret.first.begin()),
        make_move_iterator(page_ret.first.end()));
    offset = move(page_ret.second);
  } while (!offset.empty());

  co_return ret;
};



EventAsync::Task<Record> AirtableClient::get_record(
    const string& base_id,
    const string& table_name,
    const string& record_id) {
  auto response_json = co_await this->make_api_call(
      EVHTTP_REQ_GET, "/v0/" + base_id + "/" + table_name + "/" + record_id);
  co_return Record(response_json);
}



EventAsync::Task<vector<string>> AirtableClient::create_records(
    const string& base_id,
    const string& table_name,
    const vector<unordered_map<string, shared_ptr<Field>>>& contents,
    bool parse_response) {
  vector<shared_ptr<JSONObject>> record_jsons;
  for (const auto& it : contents) {
    record_jsons.emplace_back(Record::json_for_create(it));
  }
  unordered_map<string, shared_ptr<JSONObject>> root;
  root.emplace("records", new JSONObject(move(record_jsons)));
  shared_ptr<JSONObject> root_json(new JSONObject(root));

  auto response_json = co_await this->make_api_call(
      EVHTTP_REQ_POST,
      "/v0/" + base_id + "/" + table_name,
      "",
      root_json,
      parse_response);

  vector<string> ret;
  if (parse_response) {
    auto base = response_json->as_dict();
    const auto& records = base.at("records")->as_list();
    for (const auto& record_json : records) {
      ret.emplace_back(record_json->as_dict().at("id")->as_string());
    }
  }
  co_return ret;
}



EventAsync::Task<vector<Record>> AirtableClient::update_records(
    const string& base_id,
    const string& table_name,
    const unordered_map<string, unordered_map<string, shared_ptr<Field>>>& contents,
    bool parse_response) {

  vector<shared_ptr<JSONObject>> records;
  for (const auto& it : contents) {
    auto json = Record::json_for_create(it.second);
    json->as_dict().emplace("id", new JSONObject(it.first));
    records.emplace_back(json);
  }
  unordered_map<string, shared_ptr<JSONObject>> root;
  root.emplace("records", new JSONObject(move(records)));
  shared_ptr<JSONObject> root_json(new JSONObject(root));

  auto response_json = co_await this->make_api_call(
      EVHTTP_REQ_PATCH,
      "/v0/" + base_id + "/" + table_name,
      "",
      root_json,
      parse_response);

  vector<Record> ret;
  if (parse_response) {
    auto base = response_json->as_dict();
    const auto& records = base.at("records")->as_list();
    for (const auto& record_json : records) {
      ret.emplace_back(record_json);
    }
  }
  co_return ret;
}



EventAsync::Task<unordered_map<string, bool>> AirtableClient::delete_records(
    const string& base_id,
    const string& table_name,
    const vector<string>& record_ids,
    bool parse_response) {

  string query_string;
  for (const auto& record_id : record_ids) {
    if (!query_string.empty()) {
      query_string += '&';
    }
    query_string += "records[]=";
    query_string += record_id;
  }

  auto response_json = co_await this->make_api_call(
      EVHTTP_REQ_DELETE,
      "/v0/" + base_id + "/" + table_name,
      query_string,
      nullptr,
      parse_response);

  unordered_map<string, bool> ret;
  if (parse_response) {
    auto base = response_json->as_dict();
    const auto& records = base.at("records")->as_list();
    for (const auto& record_json : records) {
      ret.emplace(record_json->as_dict().at("id")->as_string(),
          record_json->as_dict().at("deleted")->as_bool());
    }
  }
  co_return ret;
}
