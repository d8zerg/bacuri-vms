#pragma once
// SOAP transport of the ONVIF adapter: XML parsing, WS-Security tokens, the camera client and the media
// service. The header is internal to the ONVIF adapter.
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/parser.hpp>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <sodium.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <thread>
#include "control/playback_time.hpp"
#include <set>
#include <sstream>
#include <utility>

namespace bacuri::vms::control::onvif
{
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
constexpr std::string_view soap = "http://www.w3.org/2003/05/soap-envelope";
constexpr std::string_view addressing = "http://schemas.xmlsoap.org/ws/2004/08/addressing";
constexpr std::string_view discovery = "http://schemas.xmlsoap.org/ws/2005/04/discovery";
constexpr std::string_view device = "http://www.onvif.org/ver10/device/wsdl";
constexpr std::string_view recording = "http://www.onvif.org/ver10/recording/wsdl";
constexpr std::string_view search = "http://www.onvif.org/ver10/search/wsdl";
constexpr std::string_view replay = "http://www.onvif.org/ver10/replay/wsdl";
constexpr std::string_view media = "http://www.onvif.org/ver10/media/wsdl";
constexpr std::string_view media2 = "http://www.onvif.org/ver20/media/wsdl";
constexpr std::string_view device_io = "http://www.onvif.org/ver10/deviceIO/wsdl";
constexpr std::string_view imaging = "http://www.onvif.org/ver20/imaging/wsdl";
constexpr std::string_view schema = "http://www.onvif.org/ver10/schema";
constexpr std::string_view ptz = "http://www.onvif.org/ver20/ptz/wsdl";
constexpr std::string_view events = "http://www.onvif.org/ver10/events/wsdl";
constexpr std::string_view wsn = "http://docs.oasis-open.org/wsn/b-2";
constexpr std::string_view wsa = "http://www.w3.org/2005/08/addressing";
constexpr std::string_view velocity_space = "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace";
constexpr std::string_view zoom_velocity_space = "http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace";
constexpr std::string_view relative_pan = "http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace";
constexpr std::string_view relative_zoom = "http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace";
using Doc = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>;
template <class T> using Object = std::unique_ptr<T, decltype(&g_object_unref)>;
using Uri = std::unique_ptr<GUri, decltype(&g_uri_unref)>;
using Chars = std::unique_ptr<gchar, decltype(&g_free)>;

inline Doc parse(const std::string& bytes)
{
    Doc doc{xmlReadMemory(bytes.data(), static_cast<int>(bytes.size()), nullptr, nullptr,
                          XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING),
            xmlFreeDoc};
    if (!doc || doc->intSubset || doc->extSubset)
        throw Failure{ErrorCode::camera_protocol_error};
    return doc;
}

inline bool named(xmlNode* n, std::string_view ns, std::string_view name)
{
    return n && n->type == XML_ELEMENT_NODE && n->ns && n->ns->href &&
           reinterpret_cast<const char*>(n->ns->href) == ns && reinterpret_cast<const char*>(n->name) == name;
}

inline xmlNode* child(xmlNode* parent, std::string_view ns, std::string_view name)
{
    for (auto* n = parent ? parent->children : nullptr; n; n = n->next)
        if (named(n, ns, name))
            return n;
    return nullptr;
}

inline std::string value(xmlNode* n)
{
    if (!n)
        throw Failure{ErrorCode::camera_protocol_error};
    std::unique_ptr<xmlChar, xmlFreeFunc> text{xmlNodeGetContent(n), xmlFree};
    if (!text || !*text || xmlStrlen(text.get()) > 4096)
        throw Failure{ErrorCode::camera_protocol_error};
    return reinterpret_cast<const char*>(text.get());
}

inline std::string escape(const std::string& text)
{
    Chars escaped{g_markup_escape_text(text.data(), static_cast<gssize>(text.size())), g_free};
    return escaped.get();
}

inline std::string uuid()
{
    Chars id{g_uuid_string_random(), g_free};
    return "urn:uuid:" + std::string{id.get()};
}

inline std::string envelope(const std::string& header, const std::string& body)
{
    return R"(<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:a="http://www.w3.org/2005/08/addressing" xmlns:w="http://schemas.xmlsoap.org/ws/2004/08/addressing" xmlns:d="http://schemas.xmlsoap.org/ws/2005/04/discovery" xmlns:dn="http://www.onvif.org/ver10/network/wsdl" xmlns:trc="http://www.onvif.org/ver10/recording/wsdl" xmlns:tse="http://www.onvif.org/ver10/search/wsdl" xmlns:trp="http://www.onvif.org/ver10/replay/wsdl" xmlns:tio="http://www.onvif.org/ver10/deviceIO/wsdl" xmlns:tds="http://www.onvif.org/ver10/device/wsdl" xmlns:trt="http://www.onvif.org/ver10/media/wsdl" xmlns:tr2="http://www.onvif.org/ver20/media/wsdl" xmlns:timg="http://www.onvif.org/ver20/imaging/wsdl" xmlns:tptz="http://www.onvif.org/ver20/ptz/wsdl" xmlns:tev="http://www.onvif.org/ver10/events/wsdl" xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2" xmlns:tt="http://www.onvif.org/ver10/schema"><s:Header>)" +
           header + "</s:Header><s:Body>" + body + "</s:Body></s:Envelope>";
}

inline std::string username_token(const Json& credentials)
{
    Chars nonce{nullptr, g_free}, hash{nullptr, g_free}, created{nullptr, g_free};
    std::array<guchar, 24> random{};
    randombytes_buf(random.data(), random.size());
    nonce.reset(g_base64_encode(random.data(), random.size()));
    std::unique_ptr<GDateTime, decltype(&g_date_time_unref)> now{g_date_time_new_now_utc(), g_date_time_unref};
    created.reset(g_date_time_format(now.get(), "%Y-%m-%dT%H:%M:%SZ"));
    const auto& password = credentials.at("password").get_ref<const std::string&>();
    std::unique_ptr<GChecksum, decltype(&g_checksum_free)> checksum{g_checksum_new(G_CHECKSUM_SHA1),
                                                                    g_checksum_free};
    g_checksum_update(checksum.get(), random.data(), random.size());
    g_checksum_update(checksum.get(), reinterpret_cast<const guchar*>(created.get()), -1);
    g_checksum_update(checksum.get(), reinterpret_cast<const guchar*>(password.data()),
                      static_cast<gssize>(password.size()));
    std::array<guchar, 20> digest{};
    gsize size = digest.size();
    g_checksum_get_digest(checksum.get(), digest.data(), &size);
    hash.reset(g_base64_encode(digest.data(), size));
    return R"(<wsse:Security xmlns:wsse="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd" xmlns:wsu="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd"><wsse:UsernameToken><wsse:Username>)" +
           escape(credentials.at("username").get_ref<const std::string&>()) +
           R"(</wsse:Username><wsse:Password Type="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest">)" +
           hash.get() +
           R"(</wsse:Password><wsse:Nonce EncodingType="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary">)" +
           nonce.get() + "</wsse:Nonce><wsu:Created>" + created.get() +
           "</wsu:Created></wsse:UsernameToken></wsse:Security>";
}

inline Uri https_uri(const std::string& url)
{
    Uri uri{g_uri_parse(url.c_str(), G_URI_FLAGS_ENCODED, nullptr), g_uri_unref};
    if (url.size() > 2048 || url.find_first_of(" \t\r\n\\") != url.npos || !uri || !url.starts_with("https://") ||
        !g_uri_get_host(uri.get()) || g_uri_get_userinfo(uri.get()) || g_uri_get_fragment(uri.get()) ||
        g_uri_get_port(uri.get()) == 0)
        throw Failure{ErrorCode::camera_transport_denied};
    return uri;
}

inline std::string ticket(const Key& key, const std::string& context, Json data)
{
    data["expires"] = g_get_real_time() / G_USEC_PER_SEC + 600;
    return seal(key, context, data.dump());
}

inline Json read_ticket(const Key& key, const std::string& context, const std::string& token)
{
    try {
        auto data = Json::parse(unseal(token, key, context));
        if (data.at("expires").get<gint64>() > g_get_real_time() / G_USEC_PER_SEC)
            return data;
    } catch (const std::exception&) {
        throw Failure{ErrorCode::camera_selection_expired};
    }
    throw Failure{ErrorCode::camera_selection_expired};
}

struct AddressingHeaders {
    std::string action, parameters;
};

struct CameraClient {
    const Json& credentials;
    std::string ip, host;
    Clock::time_point deadline;
    bool actuator{};
    Doc call(const std::string& url, std::string_view ns, const std::string& operation,
             const std::string& params = {}, const AddressingHeaders& headers = {}) const
    {
        if (Clock::now() >= deadline)
            throw Failure{ErrorCode::camera_unavailable};
        auto uri = https_uri(url);
        if (g_ascii_strcasecmp(host.c_str(), g_uri_get_host(uri.get())))
            throw Failure{ErrorCode::camera_transport_denied};
        const int declared = g_uri_get_port(uri.get());
        if (declared > 65535)
            throw Failure{ErrorCode::camera_transport_denied};
        const auto port = static_cast<guint16>(declared < 0 ? 443 : declared);
        Object<GSocketClient> client{g_socket_client_new(), g_object_unref};
        g_socket_client_set_enable_proxy(client.get(), false);
        g_socket_client_set_timeout(client.get(), 3);
        Object<GSocketConnection> tcp{
            g_socket_client_connect_to_host(client.get(), ip.c_str(), port, nullptr, nullptr), g_object_unref};
        if (!tcp)
            throw Failure{actuator ? ErrorCode::camera_command_unsent : ErrorCode::camera_unavailable};
        Object<GSocketConnectable> name{g_network_address_new(host.c_str(), port), g_object_unref};
        Object<GIOStream> tls{g_tls_client_connection_new(G_IO_STREAM(tcp.get()), name.get(), nullptr),
                              g_object_unref};
        if (!tls)
            throw Failure{ErrorCode::camera_tls_error};
        // The certificate handler reads the path during the handshake, so the string outlives it.
        const auto trust = optional_setting("BACURI_CAMERA_CA_FILE");
        if (trust) {
            const char* path = trust->c_str();
            Object<GTlsDatabase> ca{g_tls_file_database_new(path, nullptr), g_object_unref};
            if (!ca)
                throw Failure{ErrorCode::camera_tls_error};
            g_tls_connection_set_database(G_TLS_CONNECTION(tls.get()), ca.get());
            // A provisioned leaf can anchor one camera when its private issuer is unavailable.
            g_signal_connect(
                tls.get(), "accept-certificate",
                G_CALLBACK(+[](GTlsConnection* connection, GTlsCertificate* peer, GTlsCertificateFlags errors,
                               gpointer file) -> gboolean {
                    if (errors != G_TLS_CERTIFICATE_UNKNOWN_CA)
                        return false;
                    auto* identity =
                        g_tls_client_connection_get_server_identity(G_TLS_CLIENT_CONNECTION(connection));
                    if (!identity ||
                        (g_tls_certificate_verify(peer, identity, nullptr) & ~G_TLS_CERTIFICATE_UNKNOWN_CA))
                        return false;
                    auto* list = g_tls_certificate_list_new_from_file(static_cast<const char*>(file), nullptr);
                    bool trusted = false;
                    for (auto* n = list; n; n = n->next)
                        trusted |= g_tls_certificate_is_same(peer, G_TLS_CERTIFICATE(n->data));
                    g_list_free_full(list, g_object_unref);
                    return trusted;
                }),
                const_cast<char*>(path));
        }
        if (!g_tls_connection_handshake(G_TLS_CONNECTION(tls.get()), nullptr, nullptr))
            throw Failure{ErrorCode::camera_tls_error};
        const std::string action = headers.action.empty() ? std::string{ns} + "/" + operation : headers.action;
        const std::string element = (ns == device      ? "tds:"
                                     : ns == media2    ? "tr2:"
                                     : ns == media     ? "trt:"
                                     : ns == recording ? "trc:"
                                     : ns == search    ? "tse:"
                                     : ns == replay    ? "trp:"
                                     : ns == device_io ? "tio:"
                                     : ns == imaging   ? "timg:"
                                     : ns == ptz       ? "tptz:"
                                     : ns == events    ? "tev:"
                                                       : "wsnt:") +
                                    operation;
        auto body = envelope("<a:MessageID>" + uuid() + "</a:MessageID><a:To>" + escape(url) +
                                 "</a:To><a:Action>" + action +
                                 "</a:Action><a:ReplyTo><a:Address>http://www.w3.org/2005/08/addressing/"
                                 "anonymous</a:Address></a:ReplyTo>" +
                                 username_token(credentials) + headers.parameters,
                             "<" + element + ">" + params + "</" + element + ">");
        std::string target = g_uri_get_path(uri.get());
        if (target.empty())
            target = "/";
        if (const auto* query = g_uri_get_query(uri.get()))
            target += "?" + std::string(query);
        auto request = "POST " + target + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) +
                       "\r\nConnection: close\r\nContent-Type: application/soap+xml; charset=utf-8; action=\"" +
                       action + "\"\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        if (!g_output_stream_write_all(g_io_stream_get_output_stream(tls.get()), request.data(), request.size(),
                                       nullptr, nullptr, nullptr))
            throw Failure{ErrorCode::camera_unavailable};
        http::response_parser<http::string_body> parser;
        constexpr std::size_t max_body = 1024uz * 1024uz;
        parser.body_limit(max_body);
        parser.eager(true);
        boost::beast::flat_buffer buffer{2 * max_body};
        boost::system::error_code ec;
        while (!parser.is_done()) {
            if (Clock::now() >= deadline)
                throw Failure{ErrorCode::camera_unavailable};
            auto area = buffer.prepare(8192);
            const auto count = g_input_stream_read(g_io_stream_get_input_stream(tls.get()), area.data(),
                                                   area.size(), nullptr, nullptr);
            if (count < 0)
                throw Failure{ErrorCode::camera_unavailable};
            if (count == 0) {
                if (!parser.got_some())
                    throw Failure{ErrorCode::camera_protocol_error};
                parser.put_eof(ec);
                if (ec || !parser.is_done())
                    throw Failure{ErrorCode::camera_protocol_error};
                break;
            }
            buffer.commit(static_cast<std::size_t>(count));
            buffer.consume(parser.put(buffer.data(), ec));
            if (ec && ec != http::error::need_more)
                throw Failure{ErrorCode::camera_protocol_error};
            ec.clear();
        }
        const auto& response = parser.get();
        if (response.result_int() == 401 || response.result_int() == 403)
            throw Failure{ErrorCode::camera_authentication_failed};
        auto doc = parse(response.body());
        auto* root = xmlDocGetRootElement(doc.get());
        auto* content = child(root, soap, "Body");
        if (auto* fault = child(content, soap, "Fault")) {
            auto* code = child(child(child(fault, soap, "Code"), soap, "Subcode"), soap, "Value");
            if (code && value(code).ends_with("NotAuthorized"))
                throw Failure{ErrorCode::camera_authentication_failed};
            if (actuator)
                throw Failure{ErrorCode::camera_command_rejected};
        }
        if (response.result_int() != 200 || !named(root, soap, "Envelope") ||
            !child(content, ns, operation + "Response"))
            throw Failure{ErrorCode::camera_protocol_error};
        return doc;
    }
};

inline xmlNode* response(const Doc& doc, std::string_view ns, const char* operation)
{
    return child(child(xmlDocGetRootElement(doc.get()), soap, "Body"), ns, operation);
}

inline std::string attribute(xmlNode* n, const char* name)
{
    if (!n)
        return {};
    std::unique_ptr<xmlChar, xmlFreeFunc> attr{xmlGetProp(n, BAD_CAST name), xmlFree};
    return attr && xmlStrlen(attr.get()) <= 256 ? reinterpret_cast<const char*>(attr.get()) : "";
}

inline double seconds(const std::string& duration)
{
    // ONVIF timeouts are XSD durations. Only a fixed length is meaningful for movement.
    std::unique_ptr<GRegex, decltype(&g_regex_unref)> pattern{
        g_regex_new(R"(^PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+(?:\.\d+)?)S)?$)", G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT,
                    nullptr),
        g_regex_unref};
    GMatchInfo* info{};
    const bool matches = g_regex_match(pattern.get(), duration.c_str(), G_REGEX_MATCH_DEFAULT, &info);
    std::unique_ptr<GMatchInfo, decltype(&g_match_info_free)> match{info, g_match_info_free};
    if (!matches || duration.size() <= 2)
        return -1;
    auto number = [&](int index) {
        Chars part{g_match_info_fetch(info, index), g_free};
        return part ? g_ascii_strtod(part.get(), nullptr) : 0.;
    };
    return number(1) * 3600 + number(2) * 60 + number(3);
}

inline Json directions(const CameraClient& client, const std::string& url, const std::string& config)
{
    auto doc = client.call(url, ptz, "GetConfigurationOptions",
                           "<tptz:ConfigurationToken>" + escape(config) + "</tptz:ConfigurationToken>");
    auto* opts = child(response(doc, ptz, "GetConfigurationOptionsResponse"), ptz, "PTZConfigurationOptions");
    auto* timeout = child(opts, schema, "PTZTimeout");
    Json result = Json::array();
    const auto min = timeout ? seconds(value(child(timeout, schema, "Min"))) : -1;
    const auto max = timeout ? seconds(value(child(timeout, schema, "Max"))) : -1;
    const bool relative = !(min >= 0 && min <= 1 && max >= 1);
    const double step = relative ? 0.03 : 0.5;
    auto* spaces = child(opts, schema, "Spaces");
    auto bounds = [](xmlNode* range) {
        auto bound = [&](const char* field) {
            auto raw = value(child(range, schema, field));
            char* end{};
            auto number = g_ascii_strtod(raw.c_str(), &end);
            if (*end || !std::isfinite(number))
                throw Failure{ErrorCode::camera_protocol_error};
            return number;
        };
        return std::pair{bound("Min"), bound("Max")};
    };
    auto add_axis = [&](const auto& limits, const std::pair<const char*, const char*> names) {
        const auto [negative, positive] = names;
        const auto [low, high] = limits;
        if (low <= -step && high >= -step && std::ranges::find(result, Json(negative)) == result.end())
            result.push_back(negative);
        if (low <= step && high >= step && std::ranges::find(result, Json(positive)) == result.end())
            result.push_back(positive);
    };
    for (auto* n = spaces ? spaces->children : nullptr; n; n = n->next) {
        if (named(n, schema, relative ? "RelativePanTiltTranslationSpace" : "ContinuousPanTiltVelocitySpace") &&
            value(child(n, schema, "URI")) == (relative ? relative_pan : velocity_space)) {
            const auto x = bounds(child(n, schema, "XRange")), y = bounds(child(n, schema, "YRange"));
            if (y.first <= 0 && y.second >= 0)
                add_axis(x, {"left", "right"});
            if (x.first <= 0 && x.second >= 0)
                add_axis(y, {"down", "up"});
        }
        if (named(n, schema, relative ? "RelativeZoomTranslationSpace" : "ContinuousZoomVelocitySpace") &&
            value(child(n, schema, "URI")) == (relative ? relative_zoom : zoom_velocity_space))
            add_axis(bounds(child(n, schema, "XRange")), {"zoom_out", "zoom_in"});
    }
    if (!result.empty())
        result.push_back("stop");
    return {{"directions", result}, {"relative_move", relative}};
}

inline CameraClient control_client(const CameraNetworks& networks, const Json& control, const Json& credentials)
{
    auto ip = control.at("ip").get<std::string>();
    Object<GInetAddress> peer{g_inet_address_new_from_string(ip.c_str()), g_object_unref};
    if (!peer || !networks.contains(peer.get()))
        throw Failure{ErrorCode::camera_transport_denied};
    return {credentials, ip, control.at("host"), Clock::now() + 4s};
}

inline void matches(Json& found, std::set<std::string>& seen, const Doc& doc, const std::string& mid,
                    const Key& key, const std::string& site, GInetAddress* sender)
{
    Chars ip{g_inet_address_to_string(sender), g_free};
    auto* root = xmlDocGetRootElement(doc.get());
    auto* header = child(root, soap, "Header");
    if (!named(root, soap, "Envelope") ||
        value(child(header, addressing, "Action")) != std::string{discovery} + "/ProbeMatches" ||
        value(child(header, addressing, "RelatesTo")) != mid)
        return;
    const auto* list = child(child(root, soap, "Body"), discovery, "ProbeMatches");
    for (auto* m = list ? list->children : nullptr; m && found.size() < 64; m = m->next) {
        if (!named(m, discovery, "ProbeMatch"))
            continue;
        auto id = value(child(child(m, addressing, "EndpointReference"), addressing, "Address"));
        std::istringstream urls(value(child(m, discovery, "XAddrs")));
        std::string candidate;
        for (std::string url; urls >> url;) {
            try {
                if (url.starts_with("https://")) {
                    https_uri(url);
                    candidate = url;
                    break;
                }
                if (!candidate.empty() || !url.starts_with("http://"))
                    continue;
                auto uri = https_uri("https" + url.substr(4));
                if (g_uri_get_port(uri.get()) > 0 && g_uri_get_port(uri.get()) != 80)
                    continue;
                // Some devices advertise only their HTTP address even when the same service accepts TLS.
                Chars secure{g_uri_join(G_URI_FLAGS_ENCODED, "https", nullptr, g_uri_get_host(uri.get()), -1,
                                        g_uri_get_path(uri.get()), g_uri_get_query(uri.get()), nullptr),
                             g_free};
                candidate = secure.get();
            } catch (const Failure&) {
                continue;
            }
        }
        if (!candidate.empty() && seen.insert(id).second)
            found.push_back(
                {{"endpoint_id", id},
                 {"device_service", candidate},
                 {"selection", ticket(key, "probe/" + site, {{"id", id}, {"url", candidate}, {"ip", ip.get()}})}});
    }
}
struct MediaService {
    std::string address;
    std::string_view ns;
    Doc profiles(const CameraClient& client) const
    {
        return client.call(address, ns, "GetProfiles", ns == media2 ? "<tr2:Type>All</tr2:Type>" : "");
    }
    xmlNode* configuration(xmlNode* profile, const char* legacy, const char* modern) const
    {
        return ns == media2 ? child(child(profile, media2, "Configurations"), media2, modern)
                            : child(profile, schema, legacy);
    }
    std::string stream(const CameraClient& client, const std::string& token) const
    {
        const auto id = escape(token);
        const auto params =
            ns == media2 ? "<tr2:Protocol>RTSP</tr2:Protocol><tr2:ProfileToken>" + id + "</tr2:ProfileToken>"
                         : "<trt:StreamSetup><tt:Stream>RTP-Unicast</tt:Stream><tt:Transport><tt:Protocol>RTSP</"
                           "tt:Protocol></tt:Transport></trt:StreamSetup><trt:ProfileToken>" +
                               id + "</trt:ProfileToken>";
        auto doc = client.call(address, ns, "GetStreamUri", params);
        auto* root = response(doc, ns, "GetStreamUriResponse");
        return value(ns == media2 ? child(root, ns, "Uri") : child(child(root, ns, "MediaUri"), schema, "Uri"));
    }
};

inline std::string parameter(const std::string& name, const std::string& text)
{
    return "<" + name + ">" + escape(text) + "</" + name + ">";
}

inline std::string source_parameter(const Json& control)
{
    return parameter("timg:VideoSourceToken", control.at("channel"));
}

inline std::string profile_parameter(const Json& control)
{
    return parameter("tptz:ProfileToken", control.at("profile"));
}

inline std::optional<double> range_limit(xmlNode* range, const char* tag)
{
    auto* field = child(range, schema, tag);
    if (!field)
        return {};
    auto text = value(field);
    char* end = nullptr;
    double n = g_ascii_strtod(text.c_str(), &end);
    return *end || !std::isfinite(n) ? std::nullopt : std::optional(n);
}
} // namespace bacuri::vms::control::onvif
