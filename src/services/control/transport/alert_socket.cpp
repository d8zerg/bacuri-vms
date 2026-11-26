#include "application/control_api/http_json.hpp"
#include "modules/events/alert_json.hpp"
#include "services/control/transport/alert_socket.hpp"
#include "modules/events/alerts.hpp"
#include "platform/scope_exit.hpp"
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
namespace bacuri::vms::control
{
namespace
{
namespace net = boost::asio;
namespace beast = boost::beast;
namespace ws = beast::websocket;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
struct Link {
    explicit Link(std::shared_ptr<TlsStream> stream) : tls{std::move(stream)}, socket{*tls} {}
    std::shared_ptr<TlsStream> tls;
    ws::stream<TlsStream&> socket;
    bool ended{}, unexpected{};
};
void arm(net::steady_timer& timer, const std::shared_ptr<Link>& link, Clock::time_point deadline)
{
    timer.expires_at(deadline);
    timer.async_wait([weak = std::weak_ptr{link}](boost::system::error_code error) {
        if (!error) {
            if (auto active = weak.lock()) {
                beast::get_lowest_layer(active->socket).close();
            }
        }
    });
}
net::awaitable<void> monitor(std::shared_ptr<Link> link)
{
    beast::flat_buffer input;
    auto [error, count] = co_await link->socket.async_read(input, net::as_tuple(net::use_awaitable));
    link->unexpected = !error;
    link->ended = true;
}
net::awaitable<FeedBatch> poll(AlertFeed& feed, std::string_view cursor)
{
    try {
        co_return feed.poll(cursor);
    } catch (const Failure& f) {
        co_return FeedBatch{{}, static_cast<unsigned>(f.status() == 401 || f.status() == 404 ? 1008 : 1011)};
    } catch (const std::exception&) {
        co_return FeedBatch{{}, 1011};
    }
}
std::string subscription(const beast::flat_buffer& buffer)
{
    auto value = Json::parse(beast::buffers_to_string(buffer.data()), nullptr, false);
    if (!value.is_object() || value.size() != 2 || !value.contains("type") || value["type"] != "subscribe" ||
        !value.contains("after") || !value["after"].is_string()) {
        return {};
    }
    auto after = value["after"].get<std::string>();
    return after.size() <= 1024 ? after : "";
}
} // namespace
net::awaitable<void> stream_alerts(std::shared_ptr<TlsStream> transport, net::thread_pool& workers,
                                   const Request& request, std::shared_ptr<AlertFeed> feed)
{
    auto link = std::make_shared<Link>(std::move(transport));
    auto& socket = link->socket;
    auto& tcp = beast::get_lowest_layer(socket);
    platform::ScopeExit disconnect{[&tcp] { tcp.close(); }};
    tcp.expires_never();
    tcp.socket().set_option(net::socket_base::send_buffer_size{16 * 1024});
    socket.read_message_max(2048);
    socket.set_option(ws::stream_base::timeout{5s, 5s, false});
    socket.set_option(ws::stream_base::decorator{
        [](ws::response_type& res) { res.set(http::field::sec_websocket_protocol, alert_protocol); }});
    co_await socket.async_accept(request, net::use_awaitable);
    net::steady_timer timer{socket.get_executor()};
    arm(timer, link, Clock::now() + 5s);
    beast::flat_buffer first;
    co_await socket.async_read(first, net::use_awaitable);
    timer.cancel();
    const auto after = socket.got_text() ? subscription(first) : std::string{};
    unsigned close = 1008;
    if (!after.empty()) {
        socket.set_option(ws::stream_base::timeout{2s, ws::stream_base::none(), false});
        socket.text(true);
        net::co_spawn(socket.get_executor(), monitor(link), [](const std::exception_ptr&) {});

        while (!link->ended && tcp.socket().is_open()) {
            const auto now = Clock::now();
            const auto deadline = now + 2s;
            arm(timer, link, now + 5s);
            auto batch = co_await net::co_spawn(workers, poll(*feed, after), net::use_awaitable);
            if (link->ended) {
                break;
            }
            if (Clock::now() >= deadline) {
                close = 1011;
                break;
            }
            // Cancel a blocked write before this authorization decision can grow stale.
            arm(timer, link, deadline);
            for (const auto& frame : batch.frames) {
                if (Clock::now() >= deadline) {
                    close = 1013;
                    break;
                }
                const auto payload = encode_frame(frame);
                co_await socket.async_write(net::buffer(payload), net::use_awaitable);
            }
            timer.cancel();
            if (close == 1013) {
                break;
            }
            if (batch.close_code) {
                close = batch.close_code;
                break;
            }
            if (batch.frames.empty()) {
                timer.expires_after(250ms);
                co_await timer.async_wait(net::use_awaitable);
            }
        }
        if (link->ended && !link->unexpected) {
            co_return;
        }
    }
    arm(timer, link, Clock::now() + 2s);
    co_await socket.async_close(ws::close_reason{static_cast<ws::close_code>(close)},
                                net::as_tuple(net::use_awaitable));
}
} // namespace bacuri::vms::control
