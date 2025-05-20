#include "AsyncHTTPClient.hh"

#include <inttypes.h>
#include <stdlib.h>

#include <format>
#include <phosg/Strings.hh>
#include <string>
#include <vector>

#include "AsyncUtils.hh"

using namespace std;

HTTPError::HTTPError(int code, const std::string& what) : std::runtime_error(what), code(code) {}

static string url_encode(const string& s) {
  string ret;
  for (char ch : s) {
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch == '-') || (ch == '_') || (ch == '.') || (ch == '~')) {
      ret.push_back(ch);
    } else {
      ret += std::format("%{:02X}", ch);
    }
  }
  return ret;
}

static const char* name_for_method(HTTPRequest::Method method) {
  switch (method) {
    case HTTPRequest::Method::GET:
      return "GET";
    case HTTPRequest::Method::POST:
      return "POST";
    case HTTPRequest::Method::DELETE:
      return "DELETE";
    case HTTPRequest::Method::HEAD:
      return "HEAD";
    case HTTPRequest::Method::PATCH:
      return "PATCH";
    case HTTPRequest::Method::PUT:
      return "PUT";
    case HTTPRequest::Method::UPDATE:
      return "UPDATE";
    case HTTPRequest::Method::OPTIONS:
      return "OPTIONS";
    case HTTPRequest::Method::CONNECT:
      return "CONNECT";
    case HTTPRequest::Method::TRACE:
      return "TRACE";
    default:
      throw std::logic_error("Invalid request method");
  };
}

std::string HTTPRequest::serialize_without_data() const {
  string ret = std::format("{} {}", name_for_method(this->method), this->path);
  if (!this->query_params.empty()) {
    bool first = true;
    for (const auto& [k, v] : this->query_params) {
      ret += std::format("{:c}{}={}", first ? '?' : '&', url_encode(k), url_encode(v));
    }
  }
  if (!this->fragment.empty()) {
    ret += std::format("#{}", this->fragment);
  }
  ret += std::format(" {}\r\n", this->http_version);
  for (const auto& [k, v] : this->headers) {
    ret += std::format("{}: {}\r\n", k, v);
  }
  if (!this->data.empty()) {
    ret += std::format("Content-Length: {}\r\n", this->data.size());
  }
  ret += "\r\n";
  return ret;
}

const std::string* HTTPResponse::get_header(const std::string& name) const {
  auto its = this->headers.equal_range(name);
  if (its.first == its.second) {
    return nullptr;
  }
  const string* ret = &its.first->second;
  its.first++;
  if (its.first != its.second) {
    throw std::out_of_range("Header appears multiple times: " + name);
  }
  return ret;
}

AsyncHTTPClient::AsyncHTTPClient(asio::io_context& io_context)
    : io_context(io_context), ssl_context(create_default_ssl_context()) {}

template <typename SocketT>
asio::awaitable<HTTPResponse> make_request_on_stream(SocketT& stream, const HTTPRequest& req) {
  string req_str = req.serialize_without_data();

  array<asio::const_buffer, 2> bufs = {
      asio::const_buffer(req_str.data(), req_str.size()),
      asio::const_buffer(req.data.data(), req.data.size())};

  co_await asio::async_write(stream, bufs, asio::use_awaitable);

  AsyncSocketReader r(stream);

  HTTPResponse resp;
  {
    std::string response_line = co_await r.read_line("\r\n", 4096);
    size_t first_space_pos = response_line.find(' ');
    if (first_space_pos == string::npos) {
      throw std::runtime_error("Malformed response line");
    }
    resp.http_version = response_line.substr(first_space_pos);
    size_t second_space_pos = response_line.find(' ', first_space_pos + 1);
    if (second_space_pos == string::npos) {
      throw std::runtime_error("Malformed response line");
    }
    resp.response_code = stoul(response_line.substr(first_space_pos + 1, second_space_pos - first_space_pos - 1));
    resp.response_reason = response_line.substr(second_space_pos + 1);
    phosg::strip_trailing_whitespace(resp.response_reason);
  }

  auto prev_header_it = resp.headers.end();
  for (;;) {
    std::string line = co_await r.read_line("\r\n", 4096);
    if (line.empty()) {
      break;
    }
    if (line[0] == ' ' || line[0] == '\t') {
      if (prev_header_it == resp.headers.end()) {
        throw std::runtime_error("Received header continuation line before any header");
      } else {
        phosg::strip_whitespace(line);
        prev_header_it->second.append(1, ' ');
        prev_header_it->second += line;
      }
    } else {
      size_t colon_pos = line.find(':');
      if (colon_pos == string::npos) {
        throw runtime_error("Malformed header line");
      }
      string key = line.substr(0, colon_pos);
      string value = line.substr(colon_pos + 1);
      phosg::strip_whitespace(key);
      phosg::strip_whitespace(value);
      prev_header_it = resp.headers.emplace(phosg::tolower(key), std::move(value));
    }
  }

  auto transfer_encoding_header = resp.get_header("transfer-encoding");
  if (transfer_encoding_header && phosg::tolower(*transfer_encoding_header) == "chunked") {
    deque<string> chunks;
    for (;;) {
      auto line = co_await r.read_line("\r\n", 0x20);
      size_t parse_offset = 0;
      size_t chunk_size = stoull(line, &parse_offset, 16);
      if (parse_offset != line.size()) {
        throw std::runtime_error("Invalid chunk header during chunked encoding");
      }
      if (chunk_size == 0) {
        break;
      }
      chunks.emplace_back(co_await r.read_data(chunk_size));
      auto after_chunk_data = co_await r.read_line("\r\n", 0x20);
      if (!after_chunk_data.empty()) {
        throw std::runtime_error("Incorrect trailing sequence after chunk data");
      }
    }
  } else {
    auto content_length_header = resp.get_header("content-length");
    size_t content_length = content_length_header ? stoull(*content_length_header) : 0;
    if (content_length > 0) {
      resp.data = co_await r.read_data(content_length);
    }
  }

  co_return resp;
}

asio::awaitable<HTTPResponse> AsyncHTTPClient::make_request(const HTTPRequest& req) {
  if (req.https) {
    auto stream = co_await async_connect_tcp_ssl(this->io_context, this->ssl_context, req.domain, req.port, req.domain);
    co_return co_await make_request_on_stream(stream, req);
  } else {
    auto stream = co_await async_connect_tcp(req.domain, req.port);
    co_return co_await make_request_on_stream(stream, req);
  }
}
