#include "control/http.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <csignal>
#include <set>

namespace bacuri::vms::control
{
namespace
{
namespace net = boost::asio;
namespace beast = boost::beast;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
using TlsStream = ssl::stream<beast::tcp_stream>;
using namespace std::chrono_literals;

constexpr auto io_timeout = 5s;
constexpr std::size_t max_connections = 36;
constexpr unsigned max_requests_per_connection = 100;

net::awaitable<Response> process(const HttpHandler& handler, const Request& request, const std::string& peer)
{
    co_return handler(request, peer);
}

net::awaitable<MediaChunk> read_chunk(MediaBody& body)
{
    co_return body.next();
}

net::awaitable<void> send_media(TlsStream& stream, net::thread_pool& workers, Response& response)
{
    auto& transport = beast::get_lowest_layer(stream);
    transport.socket().set_option(net::socket_base::send_buffer_size{64 * 1024});
    response.content_length(response.media_size);
    http::response<http::empty_body> header{response.base()};
    auto remaining = response.media_size;
    bool started = false;
    while (remaining) {
        auto chunk = co_await net::co_spawn(workers, read_chunk(*response.media), net::use_awaitable);
        if (chunk.bytes.empty() || chunk.bytes.size() > remaining ||
            std::chrono::steady_clock::now() >= chunk.deadline) {
            throw std::runtime_error("Archive stream interrupted");
        }
        transport.expires_at(chunk.deadline);
        if (!started) {
            co_await http::async_write(stream, header, net::use_awaitable);
            started = true;
        }
        co_await net::async_write(stream, net::buffer(chunk.bytes), net::use_awaitable);
        remaining -= chunk.bytes.size();
    }
}

net::awaitable<void> exchange(TlsStream& stream, net::thread_pool& workers, const HttpHandler& handler)
{
    auto& transport = beast::get_lowest_layer(stream);
    const auto peer = transport.socket().remote_endpoint().address().to_string();
    transport.expires_after(io_timeout);
    co_await stream.async_handshake(ssl::stream_base::server, net::use_awaitable);
    beast::flat_buffer buffer;
    for (unsigned sequence = 1; sequence <= max_requests_per_connection; ++sequence) {
        http::request_parser<http::string_body> parser;
        parser.header_limit(8192);
        parser.body_limit(65536);
        transport.expires_after(io_timeout);
        auto [error, header_size] =
            co_await http::async_read_header(stream, buffer, parser, net::as_tuple(net::use_awaitable));
        if (error == http::error::end_of_stream || error == net::error::eof) {
            break;
        }

        const auto target = std::string_view{parser.get().target()};
        if (!error && !(target.starts_with("/api/v1/media-sessions/") && target.ends_with("/offer"))) {
            parser.body_limit(32768);
            if (parser.content_length().value_or(0) > 32768) {
                error = http::error::body_limit;
            }
        }
        Response response;
        auto expectation = parser.get()[http::field::expect];
        if (!error && !expectation.empty() && !beast::iequals(expectation, "100-continue")) {
            response.result(http::status::expectation_failed);
        } else {
            if (!error && !expectation.empty() && !parser.is_done()) {
                http::response<http::empty_body> interim{http::status::continue_, parser.get().version()};
                co_await http::async_write(stream, interim, net::use_awaitable);
            }
            if (!error && !parser.is_done()) {
                auto result =
                    co_await http::async_read(stream, buffer, parser, net::as_tuple(net::use_awaitable));
                error = std::get<0>(result);
            }
            if (error) {
                if (error.category() != make_error_code(http::error::bad_method).category()) {
                    break;
                }
                response.result(error == http::error::body_limit ? http::status::payload_too_large
                                : error == http::error::header_limit
                                    ? http::status::request_header_fields_too_large
                                    : http::status::bad_request);
            } else {
                const auto request = parser.release();
                // Blocking database, DNS and password operations use the bounded application pool.
                response = co_await net::co_spawn(workers, process(handler, request, peer), net::use_awaitable);
                response.version(request.version());
                response.keep_alive(request.keep_alive() && sequence < max_requests_per_connection);
                if (response.media) {
                    co_await send_media(stream, workers, response);
                } else {
                    response.prepare_payload();
                    transport.expires_after(io_timeout);
                    co_await http::async_write(stream, response, net::use_awaitable);
                }
                if (response.keep_alive()) {
                    continue;
                }
                break;
            }
        }
        response.keep_alive(false);
        response.prepare_payload();
        transport.expires_after(io_timeout);
        co_await http::async_write(stream, response, net::use_awaitable);
        break;
    }
    transport.expires_after(io_timeout);
    co_await stream.async_shutdown(net::as_tuple(net::use_awaitable));
}

using Connections = std::set<std::shared_ptr<TlsStream>>;

net::awaitable<void> accept_connections(tcp::acceptor& listener, ssl::context& tls, Connections& active,
                                        net::thread_pool& workers, const HttpHandler& handler)
{
    while (listener.is_open()) {
        auto socket = co_await listener.async_accept(net::use_awaitable);
        if (!listener.is_open()) {
            co_return;
        }
        if (active.size() >= max_connections) {
            continue;
        }
        auto stream = std::make_shared<TlsStream>(std::move(socket), tls);
        active.insert(stream);
        net::co_spawn(listener.get_executor(), exchange(*stream, workers, handler),
                      [&active, stream](std::exception_ptr) { active.erase(stream); });
    }
}
} // namespace

void listen_https(const HttpsSettings& settings, const HttpHandler& handler)
{
    net::io_context io{1};
    ssl::context tls{ssl::context::tls_server};
    if (SSL_CTX_set_min_proto_version(tls.native_handle(), TLS1_2_VERSION) != 1) {
        throw std::runtime_error("Cannot set minimum TLS version");
    }
    tls.use_certificate_chain_file(settings.certificate_chain);
    tls.use_private_key_file(settings.private_key, ssl::context::pem);
    if (SSL_CTX_check_private_key(tls.native_handle()) != 1) {
        throw std::runtime_error("Invalid TLS key pair");
    }

    net::thread_pool workers{4};
    tcp::acceptor listener{io, {net::ip::make_address(settings.address), settings.port}};
    Connections active;
    net::signal_set signals{io, SIGINT, SIGTERM};
    signals.async_wait([&](const boost::system::error_code&, int) {
        listener.close();
        for (const auto& stream : active) {
            beast::get_lowest_layer(*stream).close();
        }
    });
    std::exception_ptr listener_error;
    net::co_spawn(io, accept_connections(listener, tls, active, workers, handler), [&](std::exception_ptr error) {
        if (listener.is_open()) {
            listener_error = error;
            signals.cancel();
        }
    });
    io.run();
    workers.join();
    if (listener_error) {
        std::rethrow_exception(listener_error);
    }
}
} // namespace bacuri::vms::control
