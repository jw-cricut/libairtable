#include "AsyncUtils.hh"

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <exception>
#include <functional>
#include <optional>
#include <phosg/Strings.hh>
#include <string>

using namespace std;

asio::awaitable<asio::ip::tcp::socket> async_connect_tcp(string host, uint16_t port) {
  auto executor = co_await asio::this_coro::executor;

  asio::ip::tcp::resolver resolver(executor);
  auto endpoints = co_await resolver.async_resolve(host, std::format("{}", port), asio::use_awaitable);

  asio::ip::tcp::socket sock(executor);
  co_await asio::async_connect(sock, endpoints, asio::use_awaitable);

  co_return sock;
}

asio::ssl::context create_default_ssl_context() {
  asio::ssl::context ssl_context(asio::ssl::context::tlsv12_client);
  ssl_context.set_default_verify_paths(); // Use system CA certs
  return ssl_context;
}

asio::awaitable<asio::ssl::stream<asio::ip::tcp::socket>> async_connect_tcp_ssl(
    asio::io_context& io_context,
    asio::ssl::context& ssl_context,
    const std::string host,
    uint16_t port,
    const std::string& sni_hostname) {
  asio::ip::tcp::resolver resolver(io_context);
  asio::ssl::stream<asio::ip::tcp::socket> ssl_stream(io_context, ssl_context);

  if (!sni_hostname.empty() &&
      !SSL_set_tlsext_host_name(ssl_stream.native_handle(), sni_hostname.c_str())) {
    throw std::runtime_error("Failed to set SNI hostname");
  }

  auto endpoints = co_await resolver.async_resolve(host, std::format("{}", port));
  co_await asio::async_connect(ssl_stream.next_layer(), endpoints);
  co_await ssl_stream.async_handshake(asio::ssl::stream_base::client);
  co_return ssl_stream;
}

asio::awaitable<void> async_sleep(chrono::steady_clock::duration duration) {
  asio::steady_timer timer(co_await asio::this_coro::executor, duration);
  co_await timer.async_wait(asio::use_awaitable);
}
