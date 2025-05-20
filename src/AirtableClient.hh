#pragma once

#include <stdint.h>

#include <memory>
#include <phosg/JSON.hh>
#include <string>
#include <unordered_map>

#include "AsyncHTTPClient.hh"
#include "FieldTypes.hh"

class AirtableClient : public AsyncHTTPClient {
public:
  AirtableClient() = delete;
  AirtableClient(
      asio::io_context& io_context,
      // Your Airtable API access token
      const std::string& access_token,
      // Where to send the requests.
      const std::string& api_domain = "api.airtable.com",
      uint16_t api_port = 443);
  virtual ~AirtableClient() = default;

  AirtableClient(const AirtableClient&) = delete;
  AirtableClient(AirtableClient&&) = delete;
  AirtableClient& operator=(const AirtableClient&) = delete;
  AirtableClient& operator=(AirtableClient&&) = delete;

  // Lists up to 1000 bases accessible using this client's API key. This is a
  // metadata API function, which requires client_secret to be non-empty.
  asio::awaitable<std::vector<BaseInfo>> list_bases();

  // Returns the schema of the given base. This is a metadata API function,
  // which requires client_secret to be non-empty.
  asio::awaitable<std::unordered_map<std::string, TableSchema>> get_base_schema(const std::string& base_id);

  struct ListRecordsOptions {
    std::vector<std::string> fields; // if empty, get all fields
    std::string filter_formula; // if empty, omit from request
    size_t max_records; // if zero, no limit
    size_t page_size; // cannot be zero; default is 100
    std::vector<std::pair<std::string, bool>> sort_fields; // (field_name, ascending) pairs
    std::string view; // name or id

    ListRecordsOptions();
  };

  // Lists a page of records in a table, optionally filtering and sorting (see
  // ListRecordsOptions above). Returns a pair containing the records' contents
  // and the offset to use for the next page.
  asio::awaitable<std::pair<std::vector<Record>, std::string>> list_records_page(
      const std::string& base_id,
      const std::string& table_name,
      const ListRecordsOptions* options,
      const std::string& offset = "");

  // Like list_records_page, but automatically reads all pages.
  asio::awaitable<std::vector<Record>> list_records(const std::string& base_id, const std::string& table_name, const ListRecordsOptions* options);

  // Gets the contents of a single record.
  asio::awaitable<Record> get_record(const std::string& base_id, const std::string& table_name, const std::string& record_id);

  // Creates one or more records. Returns a list of the record IDs, in the same
  // order as the passed-in record contents maps. If you do not need the
  // returned record IDs, pass parse_response = false to skip parsing the
  // response (in this case the returned vector will always be empty).
  asio::awaitable<std::vector<std::string>> create_records(
      const std::string& base_id,
      const std::string& table_name,
      const std::vector<std::unordered_map<std::string, std::shared_ptr<Field>>>& contents,
      bool parse_response = true);

  // Updates one or more records. Returns the updated contents of the records.
  asio::awaitable<std::vector<Record>> update_records(
      const std::string& base_id,
      const std::string& table_name,
      const std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<Field>>>& contents,
      bool parse_response = true);

  // Deletes one or more records. Returns a map of {record_id: was_deleted}.
  asio::awaitable<std::unordered_map<std::string, bool>> delete_records(
      const std::string& base_id,
      const std::string& table_name,
      const std::vector<std::string>& record_ids,
      bool parse_response = true);

private:
  asio::awaitable<phosg::JSON> make_api_call(
      HTTPRequest::Method method,
      std::string&& path,
      std::unordered_multimap<std::string, std::string>&& query_params = {},
      const phosg::JSON* json = nullptr,
      bool parse_response = true);

  std::string access_token;
  std::string hostname;
  uint16_t port;
};
