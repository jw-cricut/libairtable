#pragma once

#include <event2/event.h>
#include <event2/http.h>
#include <event2/bufferevent_ssl.h>
#include <stdint.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <phosg/JSON.hh>
#include <unordered_map>
#include <string>
#include <memory>

#include <event-async/EventBase.hh>
#include <event-async/EvDNSBase.hh>
#include <event-async/HTTPRequest.hh>
#include <event-async/HTTPConnection.hh>

#include "FieldTypes.hh"



class AirtableClient {
public:
  AirtableClient() = delete;
  AirtableClient(
      // EventBase and EvDNSBase to use for making outbound requests
      EventBase& base,
      EvDNSBase& dns_base,
      // Your API key and client secret. If client secret is not provided, the
      // metadata API functions will not work.
      const std::string& api_key,
      const std::string& client_secret = "",
      // A custom SSL context to use for outbound connections. If given, the
      // caller is responsible for ensuring that it doesn't get freed before the
      // AirtableClient object. If not given, the default context is used.
      SSL_CTX* ssl_ctx = nullptr,
      // Where to send the requests.
      const std::string& api_domain = "api.airtable.com:443");
  ~AirtableClient();

  // TODO: We can probably make these objects movable without much effort.
  AirtableClient(const AirtableClient&) = delete;
  AirtableClient(AirtableClient&&) = delete;
  AirtableClient& operator=(const AirtableClient&) = delete;
  AirtableClient& operator=(AirtableClient&&) = delete;

  // Lists up to 1000 bases accessible using this client's API key. This is a
  // metadata API function, which requires client_secret to be non-empty.
  AsyncTask<std::vector<BaseInfo>> list_bases();

  // Returns the schema of the given base. This is a metadata API function,
  // which requires client_secret to be non-empty.
  AsyncTask<std::unordered_map<std::string, TableSchema>> get_base_schema(
      const std::string& base_id);

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
  AsyncTask<std::pair<std::vector<Record>, std::string>> list_records_page(
      const std::string& base_id,
      const std::string& table_name,
      const ListRecordsOptions* options,
      const std::string& offset = "");

  // Like list_records_page, but automatically reads all pages.
  AsyncTask<std::vector<Record>> list_records(
      const std::string& base_id,
      const std::string& table_name,
      const ListRecordsOptions* options);

  // Gets the contents of a single record.
  AsyncTask<Record> get_record(
      const std::string& base_id,
      const std::string& table_name,
      const std::string& record_id);

  // Creates one or more records. Returns a list of the record IDs, in the same
  // order as the passed-in record contents maps. If you do not need the
  // returned record IDs, pass parse_response = false to skip parsing the
  // response (in this case the return object will always be empty).
  AsyncTask<std::vector<std::string>> create_records(
      const std::string& base_id,
      const std::string& table_name,
      const std::vector<std::unordered_map<std::string, std::shared_ptr<Field>>>& contents,
      bool parse_response = true);

  // Updates one or more records. Returns the updated contents of the records.
  AsyncTask<std::vector<Record>> update_records(
      const std::string& base_id,
      const std::string& table_name,
      const std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<Field>>>& contents,
      bool parse_response = true);

  // Deletes one or more records. Returns a map of {record_id: was_deleted}.
  AsyncTask<std::unordered_map<std::string, bool>> delete_records(
      const std::string& base_id,
      const std::string& table_name,
      const std::vector<std::string>& record_ids,
      bool parse_response = true);

private:
  AsyncTask<std::shared_ptr<JSONObject>> make_api_call(
      evhttp_cmd_type method,
      const std::string& path,
      const std::string& query = "",
      std::shared_ptr<const JSONObject> json = nullptr,
      bool parse_response = true);

  EventBase& base;
  EvDNSBase& dns_base;
  std::string api_key;
  std::string client_secret;
  SSL_CTX* ssl_ctx;
  std::string hostname;
  uint16_t port;
  bool owned_ssl_ctx;

  size_t max_retries;
  size_t request_timeout;
};
