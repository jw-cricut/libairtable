#pragma once

#include <inttypes.h>

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <string>
#include <unordered_map>

template <typename StreamT>
class AsyncSocketReader {
public:
  explicit AsyncSocketReader(StreamT& sock) : sock(sock) {}
  AsyncSocketReader(const AsyncSocketReader&) = delete;
  AsyncSocketReader(AsyncSocketReader&&) = delete;
  AsyncSocketReader& operator=(const AsyncSocketReader&) = delete;
  AsyncSocketReader& operator=(AsyncSocketReader&&) = delete;
  ~AsyncSocketReader() = default;

  // Reads one line from the socket, buffering any extra data read. The
  // delimiter is not included in the returned line. max_length = 0 means no
  // maximum length is enforced.
  asio::awaitable<std::string> read_line(const char* delimiter = "\n", size_t max_length = 0) {
    size_t delimiter_size = strlen(delimiter);
    if (delimiter_size == 0) {
      throw std::logic_error("delimiter is empty");
    }
    size_t delimiter_backup_bytes = delimiter_size - 1;

    size_t delimiter_pos = this->pending_data.find(delimiter);
    while ((delimiter_pos == std::string::npos) && (!max_length || (this->pending_data.size() < max_length))) {
      size_t pre_size = this->pending_data.size();
      this->pending_data.resize(std::min(max_length, this->pending_data.size() + 0x400));

      auto buf = asio::buffer(this->pending_data.data() + pre_size, this->pending_data.size() - pre_size);
      size_t bytes_read = co_await this->sock.async_read_some(buf, asio::use_awaitable);
      this->pending_data.resize(pre_size + bytes_read);
      delimiter_pos = this->pending_data.find(
          delimiter,
          (delimiter_backup_bytes > pre_size) ? 0 : (pre_size - delimiter_backup_bytes));
    }

    if (delimiter_pos == std::string::npos) {
      throw std::runtime_error("line exceeds max length");
    }

    std::string ret = this->pending_data.substr(0, delimiter_pos);
    // TODO: It's not great that we copy the data here. This shouldn't be hard to fix.
    this->pending_data = this->pending_data.substr(delimiter_pos + delimiter_size);
    co_return ret;
  }

  asio::awaitable<std::string> read_data(size_t size) {
    std::string ret;
    if (this->pending_data.size() == size) {
      this->pending_data.swap(ret);
    } else if (this->pending_data.size() > size) {
      ret = this->pending_data.substr(0, size);
      this->pending_data = this->pending_data.substr(size);
    } else {
      size_t bytes_to_read = size - this->pending_data.size();
      this->pending_data.swap(ret);
      ret.resize(size);
      co_await asio::async_read(this->sock, asio::buffer(ret.data() + size - bytes_to_read, bytes_to_read), asio::use_awaitable);
    }
    co_return ret;
  }

private:
  std::string pending_data; // Data read but not yet returned to the caller
  StreamT& sock;
};

asio::ssl::context create_default_ssl_context();
asio::awaitable<asio::ip::tcp::socket> async_connect_tcp(std::string host, uint16_t port);
asio::awaitable<asio::ssl::stream<asio::ip::tcp::socket>> async_connect_tcp_ssl(
    asio::io_context& io_context,
    asio::ssl::context& ssl_context,
    const std::string host,
    uint16_t port,
    const std::string& sni_hostname);

asio::awaitable<void> async_sleep(std::chrono::steady_clock::duration duration);
