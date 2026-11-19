#pragma once

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <functional>
#include <string>

namespace bacuri::vms::control
{
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;
using HttpHandler = std::function<Response(const Request&, const std::string& peer)>;

struct HttpsSettings {
    std::string address;
    unsigned short port;
    std::string certificate_chain;
    std::string private_key;
};

void listen_https(const HttpsSettings& settings, const HttpHandler& handler);
} // namespace bacuri::vms::control
