#pragma once

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <functional>
#include <chrono>
#include <memory>
#include <string>

namespace bacuri::vms::control
{
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
struct MediaChunk {
    std::string bytes;
    std::chrono::steady_clock::time_point deadline;
};
class MediaBody
{
  public:
    virtual ~MediaBody() = default;
    virtual MediaChunk next() = 0;
};
class AlertFeed;
struct Response : http::response<http::string_body> {
    std::shared_ptr<AlertFeed> alerts;
    std::shared_ptr<MediaBody> media;
    std::uint64_t media_size{};
};
using HttpHandler = std::function<Response(const Request&, const std::string& peer)>;

struct HttpsSettings {
    std::string address;
    unsigned short port;
    std::string certificate_chain;
    std::string private_key;
};

void listen_https(const HttpsSettings& settings, const HttpHandler& handler);
} // namespace bacuri::vms::control
