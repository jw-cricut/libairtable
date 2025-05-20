#pragma once

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <map>
#include <stdexcept>
#include <string>

class HTTPError : public std::runtime_error {
public:
  HTTPError(int code, const std::string& what);
  int code;
};

struct HTTPRequest {
  enum class Method {
    GET = 0,
    POST,
    DELETE,
    HEAD,
    PATCH,
    PUT,
    UPDATE,
    OPTIONS,
    CONNECT,
    TRACE,
  };
  Method method;
  bool https = false;
  std::string domain;
  uint16_t port = 80;
  std::string path;
  std::string fragment;
  std::string http_version; // "HTTP/1.1", for example
  // Content-Length is added automatically and doesn't need to be here.
  // Content-Type is not added automatically.
  std::unordered_multimap<std::string, std::string> headers;
  std::unordered_multimap<std::string, std::string> query_params;
  std::string data;

  std::string serialize_without_data() const;
};

struct HTTPResponse {
  std::string http_version;
  int response_code = 200;
  std::string response_reason;
  std::unordered_multimap<std::string, std::string> headers;
  std::string data;

  // Gets the specified header (must be specified in all lowercase). Returns
  // nullptr if the header was not set by the server; raises std::runtime_error
  // if it appears multiple times.
  const std::string* get_header(const std::string& name) const;
};

class AsyncHTTPClient {
public:
  explicit AsyncHTTPClient(asio::io_context& io_context);
  AsyncHTTPClient(const AsyncHTTPClient&) = delete;
  AsyncHTTPClient(AsyncHTTPClient&&) = delete;
  AsyncHTTPClient& operator=(const AsyncHTTPClient&) = delete;
  AsyncHTTPClient& operator=(AsyncHTTPClient&&) = delete;
  virtual ~AsyncHTTPClient() = default;

  asio::awaitable<HTTPResponse> make_request(const HTTPRequest& req);

protected:
  asio::io_context& io_context;
  asio::ssl::context ssl_context;
};
