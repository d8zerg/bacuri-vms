#pragma once
#include "control/http.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/core/tcp_stream.hpp>
namespace bacuri::vms::control
{
using TlsStream = boost::asio::ssl::stream<boost::beast::tcp_stream>;
boost::asio::awaitable<void> stream_alerts(std::shared_ptr<TlsStream> transport,
                                           boost::asio::thread_pool& workers, const Request& request,
                                           std::shared_ptr<AlertFeed> feed);
} // namespace bacuri::vms::control
