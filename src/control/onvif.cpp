#include "control/onvif.hpp"

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
#include <thread>
#include <utility>

namespace bacuri::vms::control
{
namespace
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

Doc parse(const std::string& bytes)
{
    Doc doc{xmlReadMemory(bytes.data(), static_cast<int>(bytes.size()), nullptr, nullptr,
                          XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING),
            xmlFreeDoc};
    if (!doc || doc->intSubset || doc->extSubset)
        throw Failure{502, "camera_protocol_error"};
    return doc;
}

bool named(xmlNode* n, std::string_view ns, std::string_view name)
{
    return n && n->type == XML_ELEMENT_NODE && n->ns && n->ns->href &&
           reinterpret_cast<const char*>(n->ns->href) == ns && reinterpret_cast<const char*>(n->name) == name;
}

xmlNode* child(xmlNode* parent, std::string_view ns, std::string_view name)
{
    for (auto* n = parent ? parent->children : nullptr; n; n = n->next)
        if (named(n, ns, name))
            return n;
    return nullptr;
}

std::string value(xmlNode* n)
{
    if (!n)
        throw Failure{502, "camera_protocol_error"};
    std::unique_ptr<xmlChar, xmlFreeFunc> text{xmlNodeGetContent(n), xmlFree};
    if (!text || !*text || xmlStrlen(text.get()) > 4096)
        throw Failure{502, "camera_protocol_error"};
    return reinterpret_cast<const char*>(text.get());
}

std::string escape(const std::string& text)
{
    Chars escaped{g_markup_escape_text(text.data(), static_cast<gssize>(text.size())), g_free};
    return escaped.get();
}

std::string uuid()
{
    Chars id{g_uuid_string_random(), g_free};
    return "urn:uuid:" + std::string{id.get()};
}

std::string envelope(const std::string& header, const std::string& body)
{
    return R"(<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:a="http://www.w3.org/2005/08/addressing" xmlns:w="http://schemas.xmlsoap.org/ws/2004/08/addressing" xmlns:d="http://schemas.xmlsoap.org/ws/2005/04/discovery" xmlns:dn="http://www.onvif.org/ver10/network/wsdl" xmlns:trc="http://www.onvif.org/ver10/recording/wsdl" xmlns:tse="http://www.onvif.org/ver10/search/wsdl" xmlns:trp="http://www.onvif.org/ver10/replay/wsdl" xmlns:tio="http://www.onvif.org/ver10/deviceIO/wsdl" xmlns:tds="http://www.onvif.org/ver10/device/wsdl" xmlns:trt="http://www.onvif.org/ver10/media/wsdl" xmlns:tr2="http://www.onvif.org/ver20/media/wsdl" xmlns:timg="http://www.onvif.org/ver20/imaging/wsdl" xmlns:tptz="http://www.onvif.org/ver20/ptz/wsdl" xmlns:tev="http://www.onvif.org/ver10/events/wsdl" xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2" xmlns:tt="http://www.onvif.org/ver10/schema"><s:Header>)" +
           header + "</s:Header><s:Body>" + body + "</s:Body></s:Envelope>";
}

std::string username_token(const Json& credentials)
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

Uri https_uri(const std::string& url)
{
    Uri uri{g_uri_parse(url.c_str(), G_URI_FLAGS_ENCODED, nullptr), g_uri_unref};
    if (url.size() > 2048 || url.find_first_of(" \t\r\n\\") != url.npos || !uri || !url.starts_with("https://") ||
        !g_uri_get_host(uri.get()) || g_uri_get_userinfo(uri.get()) || g_uri_get_fragment(uri.get()) ||
        g_uri_get_port(uri.get()) == 0)
        throw Failure{422, "camera_transport_denied"};
    return uri;
}

std::string ticket(const Key& key, const std::string& context, Json data)
{
    data["expires"] = g_get_real_time() / G_USEC_PER_SEC + 600;
    return seal(key, context, data.dump());
}

Json read_ticket(const Key& key, const std::string& context, const std::string& token)
{
    try {
        auto data = Json::parse(unseal(token, key, context));
        if (data.at("expires").get<gint64>() > g_get_real_time() / G_USEC_PER_SEC)
            return data;
    } catch (const std::exception&) {
        throw Failure{400, "camera_selection_expired"};
    }
    throw Failure{400, "camera_selection_expired"};
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
            throw Failure{504, "camera_unavailable"};
        auto uri = https_uri(url);
        if (g_ascii_strcasecmp(host.c_str(), g_uri_get_host(uri.get())))
            throw Failure{422, "camera_transport_denied"};
        const int port = g_uri_get_port(uri.get()) < 0 ? 443 : g_uri_get_port(uri.get());
        Object<GSocketClient> client{g_socket_client_new(), g_object_unref};
        g_socket_client_set_enable_proxy(client.get(), false);
        g_socket_client_set_timeout(client.get(), 3);
        Object<GSocketConnection> tcp{
            g_socket_client_connect_to_host(client.get(), ip.c_str(), port, nullptr, nullptr), g_object_unref};
        if (!tcp)
            throw Failure{502, actuator ? "camera_command_unsent" : "camera_unavailable"};
        Object<GSocketConnectable> name{g_network_address_new(host.c_str(), port), g_object_unref};
        Object<GIOStream> tls{g_tls_client_connection_new(G_IO_STREAM(tcp.get()), name.get(), nullptr),
                              g_object_unref};
        if (!tls)
            throw Failure{502, "camera_tls_error"};
        const char* path = g_getenv("BACURI_CAMERA_CA_FILE");
        if (path && *path) {
            Object<GTlsDatabase> ca{g_tls_file_database_new(path, nullptr), g_object_unref};
            if (!ca)
                throw Failure{502, "camera_tls_error"};
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
            throw Failure{502, "camera_tls_error"};
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
            throw Failure{502, "camera_unavailable"};
        http::response_parser<http::string_body> parser;
        constexpr std::size_t max_body = 1024uz * 1024uz;
        parser.body_limit(max_body);
        parser.eager(true);
        boost::beast::flat_buffer buffer{2 * max_body};
        boost::system::error_code ec;
        while (!parser.is_done()) {
            if (Clock::now() >= deadline)
                throw Failure{504, "camera_unavailable"};
            auto area = buffer.prepare(8192);
            const auto count = g_input_stream_read(g_io_stream_get_input_stream(tls.get()), area.data(),
                                                   area.size(), nullptr, nullptr);
            if (count < 0)
                throw Failure{502, "camera_unavailable"};
            if (count == 0) {
                if (!parser.got_some())
                    throw Failure{502, "camera_protocol_error"};
                parser.put_eof(ec);
                if (ec || !parser.is_done())
                    throw Failure{502, "camera_protocol_error"};
                break;
            }
            buffer.commit(count);
            buffer.consume(parser.put(buffer.data(), ec));
            if (ec && ec != http::error::need_more)
                throw Failure{502, "camera_protocol_error"};
            ec.clear();
        }
        const auto& response = parser.get();
        if (response.result_int() == 401 || response.result_int() == 403)
            throw Failure{422, "camera_authentication_failed"};
        auto doc = parse(response.body());
        auto* root = xmlDocGetRootElement(doc.get());
        auto* content = child(root, soap, "Body");
        if (auto* fault = child(content, soap, "Fault")) {
            auto* code = child(child(child(fault, soap, "Code"), soap, "Subcode"), soap, "Value");
            if (code && value(code).ends_with("NotAuthorized"))
                throw Failure{422, "camera_authentication_failed"};
            if (actuator)
                throw Failure{422, "camera_command_rejected"};
        }
        if (response.result_int() != 200 || !named(root, soap, "Envelope") ||
            !child(content, ns, operation + "Response"))
            throw Failure{502, "camera_protocol_error"};
        return doc;
    }
};

xmlNode* response(const Doc& doc, std::string_view ns, const char* operation)
{
    return child(child(xmlDocGetRootElement(doc.get()), soap, "Body"), ns, operation);
}

std::string attribute(xmlNode* n, const char* name)
{
    if (!n)
        return {};
    std::unique_ptr<xmlChar, xmlFreeFunc> attr{xmlGetProp(n, BAD_CAST name), xmlFree};
    return attr && xmlStrlen(attr.get()) <= 256 ? reinterpret_cast<const char*>(attr.get()) : "";
}

double seconds(const std::string& duration)
{
    // ONVIF timeouts are XSD durations. Only a fixed length is meaningful for movement.
    std::unique_ptr<GRegex, decltype(&g_regex_unref)> pattern{
        g_regex_new("^PT(?:(\\d+)H)?(?:(\\d+)M)?(?:(\\d+(?:\\.\\d+)?)S)?$", G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT,
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

Json directions(const CameraClient& client, const std::string& url, const std::string& config)
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
                throw Failure{502, "camera_protocol_error"};
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

CameraClient control_client(const CameraNetworks& networks, const Json& control, const Json& credentials)
{
    auto ip = control.at("ip").get<std::string>();
    Object<GInetAddress> peer{g_inet_address_new_from_string(ip.c_str()), g_object_unref};
    if (!peer || !networks.contains(peer.get()))
        throw Failure{422, "camera_transport_denied"};
    return {credentials, ip, control.at("host"), Clock::now() + 4s};
}

void matches(Json& found, std::set<std::string>& seen, const Doc& doc, const std::string& mid, const Key& key,
             const std::string& site, GInetAddress* sender)
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

} // namespace

Json discover_onvif(const CameraNetworks& networks, const Key& key, const std::string& site)
{
    const auto mid = uuid();
    const auto probe =
        envelope("<w:MessageID>" + mid +
                     "</w:MessageID><w:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To><w:Action>" +
                     std::string(discovery) + "/Probe</w:Action>",
                 "<d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types></d:Probe>");
    Object<GSocket> sock{
        g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, nullptr),
        g_object_unref};
    if (!sock)
        throw Failure{503, "camera_unavailable"};
    const int fd = g_socket_get_fd(sock.get());
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(3702);
    inet_pton(AF_INET, "239.255.255.250", &dest.sin_addr);
    ifaddrs* addresses{};
    if (getifaddrs(&addresses) != 0)
        throw Failure{503, "camera_unavailable"};
    std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> interfaces{addresses, freeifaddrs};
    bool sent = false;
    for (auto* entry = addresses; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET)
            continue;
        const auto address = reinterpret_cast<sockaddr_in*>(entry->ifa_addr)->sin_addr;
        Object<GInetAddress> ip{
            g_inet_address_new_from_bytes(reinterpret_cast<const guint8*>(&address), G_SOCKET_FAMILY_IPV4),
            g_object_unref};
        if (!networks.contains(ip.get()))
            continue;
        if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &address, sizeof address) == 0 &&
            sendto(fd, probe.data(), probe.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof dest) > 0)
            sent = true;
    }
    for (const auto& host : networks.discovery_hosts()) {
        if (inet_pton(AF_INET, host.c_str(), &dest.sin_addr) == 1 &&
            sendto(fd, probe.data(), probe.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof dest) > 0)
            sent = true;
    }
    if (!sent)
        throw Failure{503, "camera_discovery_unavailable"};
    Json found = Json::array();
    std::set<std::string> seen;
    const auto end = Clock::now() + 1200ms;
    pollfd poller{fd, POLLIN, 0};
    while (Clock::now() < end && found.size() < 64) {
        if (poll(&poller, 1,
                 static_cast<int>(std::max(
                     0L, std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count()))) <= 0)
            break;
        sockaddr_in peer{};
        socklen_t size = sizeof peer;
        std::array<char, 65536> buffer{};
        auto n = recvfrom(fd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&peer), &size);
        if (n <= 0)
            continue;
        Object<GInetAddress> ip{
            g_inet_address_new_from_bytes(reinterpret_cast<const guint8*>(&peer.sin_addr), G_SOCKET_FAMILY_IPV4),
            g_object_unref};
        if (!networks.contains(ip.get()))
            continue;
        try {
            matches(found, seen, parse(std::string(buffer.data(), n)), mid, key, site, ip.get());
        } catch (const Failure&) {
            continue;
        }
    }
    return found;
}

Json inspect_onvif(const CameraNetworks& networks, const Key& key, const std::string& site, const Json& body)
{
    auto selection = read_ticket(key, "probe/" + site, string_field(body, "selection", 16384));
    string_field(body, "username", 128);
    string_field(body, "password", 1024, true);
    const std::string url = selection.at("url"), ip = selection.at("ip");
    auto uri = https_uri(url);
    Object<GInetAddress> peer{g_inet_address_new_from_string(ip.c_str()), g_object_unref};
    if (!peer || !networks.contains(peer.get()))
        throw Failure{422, "camera_transport_denied"};
    CameraClient client{body, ip, g_uri_get_host(uri.get()), Clock::now() + 15s};
    auto info = client.call(url, device, "GetDeviceInformation");
    auto* identity = response(info, device, "GetDeviceInformationResponse");
    Json result{{"endpoint_id", selection.at("id")},
                {"model", value(child(identity, device, "Model"))},
                {"manufacturer", value(child(identity, device, "Manufacturer"))},
                {"firmware", value(child(identity, device, "FirmwareVersion"))},
                {"serial", value(child(identity, device, "SerialNumber"))}};
    auto caps = client.call(url, device, "GetCapabilities", "<tds:Category>All</tds:Category>");
    auto* services = child(response(caps, device, "GetCapabilitiesResponse"), device, "Capabilities");
    auto* legacy_media = child(child(services, schema, "Media"), schema, "XAddr");
    MediaService service{legacy_media ? value(legacy_media) : "", media};
    result["ptz"] = child(services, schema, "PTZ") != nullptr;
    result["events"] = child(services, schema, "Events") != nullptr;
    Json options = Json::array();
    Json control{{"ip", ip}, {"host", client.host}, {"device_service", url}};
    for (const auto* name : {"PTZ", "Events", "Imaging"}) {
        if (auto* address = child(child(services, schema, name), schema, "XAddr"))
            control[name] = value(address);
    }
    if (auto* address = child(child(child(services, schema, "Extension"), schema, "DeviceIO"), schema, "XAddr"))
        control["DeviceIO"] = value(address);
    bool audio_output = false;
    try {
        auto list = client.call(url, device, "GetServices", "<tds:IncludeCapability>true</tds:IncludeCapability>");
        auto* root = response(list, device, "GetServicesResponse");
        for (auto* item = root->children; item; item = item->next) {
            if (!named(item, device, "Service"))
                continue;
            const auto ns = value(child(item, device, "Namespace")), address = value(child(item, device, "XAddr"));
            if (ns == media2 && address.starts_with("https://")) {
                auto uri = https_uri(address);
                if (!g_ascii_strcasecmp(client.host.c_str(), g_uri_get_host(uri.get())))
                    service = {address, media2};
            }
            if (ns == device_io) {
                auto* io = child(child(item, device, "Capabilities"), device_io, "Capabilities");
                audio_output = g_ascii_strtoull(attribute(io, "AudioOutputs").c_str(), nullptr, 10) > 0;
            }
            for (auto [name, space] :
                 {std::pair{"Events", events}, {"Imaging", imaging}, {"PTZ", ptz}, {"DeviceIO", device_io}}) {
                if (space != ns || !address.starts_with("https://"))
                    continue;
                auto endpoint = https_uri(address);
                if (!g_ascii_strcasecmp(client.host.c_str(), g_uri_get_host(endpoint.get())))
                    control[name] = address;
            }
        }
    } catch (const Failure& error) {
        // Earlier devices only implement GetCapabilities.
        if (error.code != "camera_protocol_error")
            throw;
    }
    if (control.contains("PTZ")) {
        try {
            auto reply = client.call(control.at("PTZ"), ptz, "GetServiceCapabilities");
            auto* capabilities =
                child(response(reply, ptz, "GetServiceCapabilitiesResponse"), ptz, "Capabilities");
            const auto flag = attribute(capabilities, "StatusPosition");
            if (flag == "true" || flag == "1" || flag == "false" || flag == "0")
                control["status_position"] = flag == "true" || flag == "1";
        } catch (const Failure&) {
            // Older services omit capability discovery. GetStatus can still report a position.
            control["status_position"] = nullptr;
        }
    }
    if (auto* supported = child(child(services, schema, "Events"), schema, "WSPullPointSupport")) {
        auto flag = value(supported);
        if (flag != "true" && flag != "1")
            control.erase("Events");
    }
    if (service.address.empty())
        throw Failure{422, "camera_incompatible_profiles"};
    Doc profiles{nullptr, xmlFreeDoc};
    try {
        profiles = service.profiles(client);
    } catch (const Failure& err) {
        if (service.ns != media2 || !legacy_media || err.code != "camera_protocol_error")
            throw;
        service = {value(legacy_media), media};
        profiles = service.profiles(client);
    }
    control["media_service"] = service.address;
    if (service.ns == media2)
        control["media2"] = true;
    Json controls = Json::object();
    std::map<std::string, Json> ptz_options;
    std::set<std::string> tokens, addresses;
    bool denied_transport = false;
    for (auto* p = response(profiles, service.ns, "GetProfilesResponse")->children; p; p = p->next) {
        if (!named(p, service.ns, "Profiles"))
            continue;
        auto* enc = service.configuration(p, "VideoEncoderConfiguration", "VideoEncoder");
        const auto encoding = enc ? value(child(enc, schema, "Encoding")) : "";
        if (encoding != "H264" && encoding != "H265" && encoding != "HEVC")
            continue;
        std::unique_ptr<xmlChar, xmlFreeFunc> token{xmlGetProp(p, BAD_CAST "token"), xmlFree};
        if (!token || xmlStrlen(token.get()) > 128 || !*token)
            throw Failure{502, "camera_protocol_error"};
        const std::string id = reinterpret_cast<const char*>(token.get());
        if (!tokens.insert(id).second || tokens.size() > 16)
            throw Failure{502, "camera_protocol_error"};
        auto address = service.stream(client, id);
        Uri stream_uri{g_uri_parse(address.c_str(), G_URI_FLAGS_ENCODED, nullptr), g_uri_unref};
        if (!stream_uri ||
            g_ascii_strcasecmp(client.host.c_str(),
                               g_uri_get_host(stream_uri.get()) ? g_uri_get_host(stream_uri.get()) : "") ||
            !networks.permits(address)) {
            denied_transport = true;
            continue;
        }
        if (!addresses.insert(address).second)
            continue;
        Json selected{{"profile", id}, {"directions", Json::array()}};
        selected["audio_input"] = service.configuration(p, "AudioSourceConfiguration", "AudioSource") != nullptr;
        auto* io = child(child(services, schema, "Extension"), schema, "DeviceIO");
        selected["audio_output"] =
            audio_output || service.configuration(p, "AudioOutputConfiguration", "AudioOutput") ||
            child(child(p, schema, "Extension"), schema, "AudioOutputConfiguration") ||
            (child(io, schema, "AudioOutputs") &&
             g_ascii_strtoull(value(child(io, schema, "AudioOutputs")).c_str(), nullptr, 10) > 0);
        auto* source = service.configuration(p, "VideoSourceConfiguration", "VideoSource");
        selected["source"] = attribute(source, "token");
        auto* physical_source = child(source, schema, "SourceToken");
        selected["channel"] = physical_source ? value(physical_source) : selected.at("source").get<std::string>();
        selected["imaging_source"] = physical_source != nullptr;
        auto config = attribute(service.configuration(p, "PTZConfiguration", "PTZ"), "token");
        selected["ptz_profile"] = !config.empty();
        if (!config.empty() && control.contains("PTZ")) {
            auto* cfg = service.configuration(p, "PTZConfiguration", "PTZ");
            if (auto* node = child(cfg, schema, "NodeToken"))
                selected["node"] = value(node);
            for (auto [field, element] : {std::pair{"pan_space", "DefaultAbsolutePantTiltPositionSpace"},
                                          std::pair{"zoom_space", "DefaultAbsoluteZoomPositionSpace"}})
                if (auto* node = child(cfg, schema, element))
                    selected[field] = value(node);
            if (!ptz_options.contains(config)) {
                try {
                    ptz_options[config] = directions(client, control.at("PTZ"), config);
                } catch (const Failure&) {
                    ptz_options[config] = {{"directions", Json::array()}};
                }
            }
            selected.update(ptz_options.at(config));
        }
        Json option{{"token", id},
                    {"name", value(child(p, service.ns == media2 ? media2 : schema, "Name"))},
                    {"url", address},
                    {"channel", selected.at("channel")},
                    {"codec", encoding == "H264" ? "h264" : "h265"}};
        if (auto* resolution = child(enc, schema, "Resolution")) {
            auto dimension = [&](const char* name) {
                const auto s = value(child(resolution, schema, name));
                char* end = nullptr;
                auto n = g_ascii_strtoull(s.c_str(), &end, 10);
                if (*end || n == 0 || n > 65535)
                    throw Failure{502, "camera_protocol_error"};
                return n;
            };
            option["width"] = dimension("Width");
            option["height"] = dimension("Height");
        }
        selected["stream_url"] = address;
        selected["codec"] = option.at("codec");
        selected["pixels"] = option.value("width", 0LL) * option.value("height", 0LL);
        controls[fingerprint(key, "onvif-stream", address)] = std::move(selected);
        options.push_back(std::move(option));
    }
    if (options.size() < 2)
        throw Failure{422, denied_transport ? "camera_transport_denied" : "camera_incompatible_profiles"};
    Json enrollment = result;
    enrollment["control"] = std::move(control);
    enrollment["profiles"] = std::move(controls);
    result["profiles"] = std::move(options);
    result["selection"] = ticket(key, "camera/" + site, std::move(enrollment));
    if (result.at("selection").get_ref<const std::string&>().size() > 24576)
        throw Failure{502, "camera_protocol_error"};
    return result;
}

void verify_camera_channel(const CameraNetworks& networks, const Json& config)
{
    const auto& control = config.at("onvif_control");
    auto client = control_client(networks, control, config.at("credentials"));
    auto info = client.call(control.at("device_service"), device, "GetDeviceInformation");
    auto* identity = response(info, device, "GetDeviceInformationResponse");
    const auto& expected = config.at("onvif");
    if (value(child(identity, device, "SerialNumber")) != expected.at("serial").get_ref<const std::string&>() ||
        value(child(identity, device, "Model")) != expected.at("model").get_ref<const std::string&>() ||
        value(child(identity, device, "FirmwareVersion")) != expected.at("firmware").get_ref<const std::string&>())
        throw Failure{409, "camera_binding_conflict"};
    MediaService service{control.at("media_service"), control.value("media2", false) ? media2 : media};
    auto profiles = service.profiles(client);
    std::set<std::string> compatible;
    for (auto* p = response(profiles, service.ns, "GetProfilesResponse")->children; p; p = p->next) {
        if (!named(p, service.ns, "Profiles"))
            continue;
        auto* enc = service.configuration(p, "VideoEncoderConfiguration", "VideoEncoder");
        const auto encoding = enc ? value(child(enc, schema, "Encoding")) : "";
        if (encoding != "H264" && encoding != "H265" && encoding != "HEVC")
            continue;
        auto* source = service.configuration(p, "VideoSourceConfiguration", "VideoSource");
        auto* token = child(source, schema, "SourceToken");
        if ((token ? value(token) : attribute(source, "token")) ==
            control.at("channel").get_ref<const std::string&>())
            compatible.insert(attribute(p, "token"));
    }
    for (auto [profile, url] :
         {std::pair{"profile", "main_url"}, std::pair{"secondary_profile", "secondary_url"}}) {
        if (!compatible.contains(control.at(profile).get_ref<const std::string&>()))
            throw Failure{409, "camera_binding_conflict"};
        auto uri = service.stream(client, control.at(profile));
        if (uri != config.at(url).get_ref<const std::string&>() || !networks.permits(uri))
            throw Failure{409, "camera_binding_conflict"};
    }
}

Json camera_brightness(const CameraNetworks& networks, const Json& control, const Json& credentials)
{
    if (!control.contains("Imaging") || !control.value("imaging_source", false))
        return nullptr;
    auto client = control_client(networks, control, credentials);
    const auto source = "<timg:VideoSourceToken>" + escape(control.at("channel").get_ref<const std::string&>()) +
                        "</timg:VideoSourceToken>";
    auto options = client.call(control.at("Imaging"), imaging, "GetOptions", source);
    auto* range = child(child(response(options, imaging, "GetOptionsResponse"), imaging, "ImagingOptions"), schema,
                        "Brightness");
    if (!range)
        return nullptr;
    auto read = [](xmlNode* n) {
        auto raw = value(n);
        char* end = nullptr;
        auto v = g_ascii_strtod(raw.c_str(), &end);
        if (*end || !std::isfinite(v))
            throw Failure{502, "camera_protocol_error"};
        return v;
    };
    auto settings = client.call(control.at("Imaging"), imaging, "GetImagingSettings", source);
    auto* values = child(response(settings, imaging, "GetImagingSettingsResponse"), imaging, "ImagingSettings");
    return {{"min", read(child(range, schema, "Min"))},
            {"max", read(child(range, schema, "Max"))},
            {"value", read(child(values, schema, "Brightness"))}};
}

double apply_camera_brightness(const CameraNetworks& networks, const Json& config, BrightnessChange change)
{
    const auto& control = config.at("onvif_control");
    const auto& credentials = config.at("credentials");
    auto matches = [](const Json& value, double target) {
        return value.get<float>() == static_cast<float>(target);
    };
    auto current = camera_brightness(networks, control, credentials);
    if (current.is_null() || change.requested < current.at("min").get<double>() ||
        change.requested > current.at("max").get<double>())
        throw Failure{422, "camera_image_unsupported"};
    if (matches(current.at("value"), change.requested))
        return current.at("value").get<double>();
    if (!matches(current.at("value"), change.expected))
        throw Failure{409, "revision_conflict"};
    auto client = control_client(networks, control, credentials);
    client.call(control.at("Imaging"), imaging, "SetImagingSettings",
                "<timg:VideoSourceToken>" + escape(control.at("channel").get_ref<const std::string&>()) +
                    "</timg:VideoSourceToken><timg:ImagingSettings><tt:Brightness>" +
                    Json(change.requested).dump() +
                    "</tt:Brightness></timg:ImagingSettings><timg:ForcePersistence>true</timg:ForcePersistence>");
    auto actual = camera_brightness(networks, control, credentials);
    if (actual.is_null() || !matches(actual.at("value"), change.requested))
        throw Failure{502, "camera_image_unconfirmed"};
    return actual.at("value").get<double>();
}

Json read_ptz_position(const CameraNetworks& networks, const Json& control, const Json& credentials)
{
    auto client = control_client(networks, control, credentials);
    auto doc = client.call(control.at("PTZ"), ptz, "GetStatus",
                           "<tptz:ProfileToken>" + escape(control.at("profile").get_ref<const std::string&>()) +
                               "</tptz:ProfileToken>");
    auto* status = child(response(doc, ptz, "GetStatusResponse"), ptz, "PTZStatus");
    if (!status || (child(status, schema, "Error") && child(status, schema, "Error")->children))
        throw Failure{502, "camera_protocol_error"};
    auto* position = child(status, schema, "Position");
    auto* pan = child(position, schema, "PanTilt");
    auto number = [](xmlNode* node, const char* key) -> std::optional<double> {
        auto text = attribute(node, key);
        if (text.empty())
            return {};
        char* end{};
        double n = g_ascii_strtod(text.c_str(), &end);
        if (*end || !std::isfinite(n))
            throw Failure{502, "camera_protocol_error"};
        return n;
    };
    auto space = attribute(pan, "space");
    if (space.empty())
        space = control.value("pan_space", std::string{});
    auto x = number(pan, "x");
    if (x && std::abs(*x) > 1e9)
        throw Failure{502, "camera_protocol_error"};
    if (!x || space.empty())
        return nullptr;
    Json sample{{"pan", *x}, {"space", space}};
    if (auto tilt = number(pan, "y"))
        sample["tilt"] = *tilt;
    auto* motion = child(status, schema, "MoveStatus");
    bool moving = false, idle = false, unknown = false;
    for (const auto* axis : {"PanTilt", "Zoom"}) {
        if (auto* state = child(motion, schema, axis)) {
            const auto reported = value(state);
            moving |= reported == "MOVING";
            idle |= reported == "IDLE";
            unknown |= reported != "IDLE" && reported != "MOVING";
        }
    }
    if (moving || (idle && !unknown))
        sample["moving"] = moving;
    if (auto fov = number(child(status, schema, "FieldOfView"), "hfov"); fov && *fov > 0 && *fov < 180)
        sample["hfov"] = *fov;
    auto* zoom = child(position, schema, "Zoom");
    auto zspace = attribute(zoom, "space");
    if (zspace.empty())
        zspace = control.value("zoom_space", std::string{});
    if (auto z = number(zoom, "x");
        z && *z >= 0 && *z <= 1 && zspace == "http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace")
        sample["zoom"] = *z;
    const auto stamp = value(child(status, schema, "UtcTime"));
    Timestamp parsed;
    if (!ProtoTime::FromString(stamp, &parsed))
        throw Failure{502, "camera_protocol_error"};
    sample["source_time"] = ProtoTime::ToString(parsed);
    return sample;
}

namespace
{
std::string parameter(const std::string& name, const std::string& text)
{
    return "<" + name + ">" + escape(text) + "</" + name + ">";
}

std::string source_parameter(const Json& control)
{
    return parameter("timg:VideoSourceToken", control.at("channel"));
}

std::string profile_parameter(const Json& control)
{
    return parameter("tptz:ProfileToken", control.at("profile"));
}

std::optional<double> range_limit(xmlNode* range, const char* tag)
{
    auto* field = child(range, schema, tag);
    if (!field)
        return {};
    auto text = value(field);
    char* end = nullptr;
    double n = g_ascii_strtod(text.c_str(), &end);
    return *end || !std::isfinite(n) ? std::nullopt : std::optional(n);
}
} // namespace

Json read_edge_archive(const CameraNetworks& networks, const Json& control, const Json& credentials,
                       const Key& key, EdgeSelection* selected)
{
    auto client = control_client(networks, control, credentials);
    Json result{{"supported", false}, {"recordings", Json::array()}};
    auto services = client.call(control.at("device_service"), device, "GetServices",
                                "<tds:IncludeCapability>false</tds:IncludeCapability>");
    std::map<std::string_view, std::string> endpoints;
    auto* root = response(services, device, "GetServicesResponse");
    if (!root)
        throw Failure{502, "camera_protocol_error"};
    for (auto* item = root->children; item; item = item->next) {
        if (!named(item, device, "Service"))
            continue;
        auto ns = value(child(item, device, "Namespace"));
        for (auto expected : {recording, search, replay}) {
            if (ns != expected)
                continue;
            const auto address = value(child(item, device, "XAddr"));
            if (!address.starts_with("https://"))
                continue;
            auto uri = https_uri(address);
            if (!g_ascii_strcasecmp(client.host.c_str(), g_uri_get_host(uri.get())))
                endpoints[expected] = address;
        }
    }
    if (!endpoints.contains(recording) || !endpoints.contains(search) || !endpoints.contains(replay))
        return result;
    result["supported"] = true;
    auto list = client.call(endpoints.at(recording), recording, "GetRecordings");
    auto* body = response(list, recording, "GetRecordingsResponse");
    if (!body)
        throw Failure{502, "camera_protocol_error"};
    std::set<std::string> seen;
    for (auto* item = body->children; item; item = item->next) {
        if (!named(item, recording, "RecordingItem"))
            continue;
        auto token = value(child(item, recording, "RecordingToken"));
        if (seen.size() >= 64 || token.size() > 256 || !seen.insert(token).second)
            throw Failure{502, "camera_protocol_error"};
        auto info = client.call(endpoints.at(search), search, "GetRecordingInformation",
                                "<tse:RecordingToken>" + escape(token) + "</tse:RecordingToken>");
        auto* record =
            child(response(info, search, "GetRecordingInformationResponse"), search, "RecordingInformation");
        if (!record)
            throw Failure{502, "camera_protocol_error"};
        Json tracks = Json::array();
        for (auto* track = record->children; track; track = track->next) {
            if (!named(track, schema, "Track"))
                continue;
            auto kind = value(child(track, schema, "TrackType"));
            if (kind != "Video" && kind != "Audio" && kind != "Metadata")
                continue;
            Json bounds = nullptr;
            auto* from = child(track, schema, "DataFrom");
            auto* to = child(track, schema, "DataTo");
            if (from && to) {
                Timestamp a, b;
                if (!ProtoTime::FromString(value(from), &a) || !ProtoTime::FromString(value(to), &b))
                    throw Failure{502, "camera_protocol_error"};
                if (ProtoTime::TimestampToMicroseconds(a) < ProtoTime::TimestampToMicroseconds(b))
                    bounds = Window{ProtoTime::TimestampToMicroseconds(a), ProtoTime::TimestampToMicroseconds(b)}
                                 .json();
            }
            if (tracks.size() >= 64)
                throw Failure{502, "camera_protocol_error"};
            tracks.push_back({{"kind", kind}, {"bounds", bounds}});
        }
        const auto id =
            fingerprint(key, "edge-recording", Json::array({client.host, control.at("source"), token}).dump());
        auto* origin = child(record, schema, "Source");
        auto* name = child(origin, schema, "Name");
        const auto label = name ? value(name).substr(0, 256) : std::string{};
        result["recordings"].push_back({{"id", id}, {"name", label}, {"tracks", tracks}});
        if (selected && selected->id == id) {
            if (std::ranges::count_if(tracks, [](const Json& t) { return t.at("kind") == "Video"; }) != 1)
                throw Failure{422, "edge_recording_unavailable"};
            const auto reply =
                client.call(endpoints.at(replay), replay, "GetReplayUri",
                            "<trp:StreamSetup><tt:Stream>RTP-Unicast</tt:Stream><tt:Transport><tt:Protocol>RTSP</"
                            "tt:Protocol></tt:Transport></trp:StreamSetup>"
                            "<trp:RecordingToken>" +
                                escape(token) + "</trp:RecordingToken>");
            const auto uri = value(child(response(reply, replay, "GetReplayUriResponse"), replay, "Uri"));
            Uri parsed{g_uri_parse(uri.c_str(), G_URI_FLAGS_ENCODED, nullptr), g_uri_unref};
            if (!parsed || !g_uri_get_host(parsed.get()) ||
                g_ascii_strcasecmp(client.host.c_str(), g_uri_get_host(parsed.get())) || !networks.permits(uri))
                throw Failure{422, "camera_transport_denied"};
            selected->uri = uri;
            const auto before = g_get_real_time();
            const auto clock = client.call(control.at("device_service"), device, "GetSystemDateAndTime");
            const auto after = g_get_real_time();
            auto* utc =
                child(child(response(clock, device, "GetSystemDateAndTimeResponse"), device, "SystemDateAndTime"),
                      schema, "UTCDateTime");
            auto number = [&](const char* part, const char* field) {
                const auto v = value(child(child(utc, schema, part), schema, field));
                if (v.size() > 4 || v.find_first_not_of("0123456789") != v.npos)
                    throw Failure{502, "camera_protocol_error"};
                return std::stoi(v);
            };
            std::unique_ptr<GDateTime, decltype(&g_date_time_unref)> time{
                g_date_time_new_utc(number("Date", "Year"), number("Date", "Month"), number("Date", "Day"),
                                    number("Time", "Hour"), number("Time", "Minute"), number("Time", "Second")),
                g_date_time_unref};
            if (!time || after < before || after - before > 4000000)
                throw Failure{409, "camera_clock_uncertain"};
            selected->offset_us =
                before + (after - before) / 2 - (g_date_time_to_unix(time.get()) * G_USEC_PER_SEC + 500000);
            selected->uncertainty_us = 500000 + (after - before + 1) / 2;
            // Do not shift UTC-aligned cameras by the arbitrary phase of a whole-second clock report.
            if (std::abs(selected->offset_us) <= selected->uncertainty_us) {
                selected->uncertainty_us += std::abs(selected->offset_us);
                selected->offset_us = 0;
            }
        }
    }
    return result;
}

Json read_camera_controls(const CameraNetworks& networks, const Json& control, const Json& credentials)
{
    auto client = control_client(networks, control, credentials);
    client.deadline = Clock::now() + 12s;
    Json actions = Json::array(), inputs = Json::array();
    auto add = [&](std::string_view command, const std::string& target = {}, std::string title = {}) -> Json& {
        return actions.emplace_back(
            Json{{"command", command}, {"target", target}, {"label", title.empty() ? target : title}});
    };
    auto optional = [&](const char* service, std::string_view ns, const char* op, const std::string& params,
                        const auto& consume, const AddressingHeaders& headers = {}) {
        if (!control.contains(service))
            return;
        try {
            auto doc = client.call(control.at(service), ns, op, params, headers);
            consume(response(doc, ns, (std::string(op) + "Response").c_str()));
        } catch (const Failure& error) {
            // An optional service may not implement an operation. Transport and trust failures are actionable.
            if (error.code != "camera_protocol_error")
                throw;
        }
    };
    if (control.value("imaging_source", false)) {
        optional("Imaging", imaging, "GetMoveOptions", source_parameter(control), [&](xmlNode* root) {
            auto* options = child(root, imaging, "MoveOptions");
            for (const auto* mode : {"Relative", "Continuous"}) {
                const bool relative = std::string_view(mode) == "Relative";
                auto* range = child(child(options, schema, mode), schema, relative ? "Distance" : "Speed");
                auto low = range_limit(range, "Min"), high = range_limit(range, "Max");
                if (!low || !high || *low > *high || *low > 0 || *high < 0 || *low == *high)
                    continue;
                for (bool near : {true, false}) {
                    const double speed = (near ? *low : *high) * (relative ? 0.05 : 0.3);
                    if (speed == 0)
                        continue;
                    auto& action = add(near ? "focus_near" : "focus_far");
                    action["mode"] = mode;
                    action["amount"] = speed;
                }
                add("focus_stop");
                break;
            }
        });
    }
    if (control.value("ptz_profile", false)) {
        const auto profile = profile_parameter(control);
        optional("PTZ", ptz, "GetPresets", profile, [&](xmlNode* root) {
            for (auto* n = root->children; n; n = n->next) {
                const auto token = attribute(n, "token");
                if (named(n, ptz, "Preset") && !token.empty())
                    add("preset_goto", token, child(n, schema, "Name") ? value(child(n, schema, "Name")) : token);
            }
        });
        optional("PTZ", ptz, "GetPresetTours", profile, [&](xmlNode* root) {
            for (auto* n = root->children; n; n = n->next) {
                const auto token = attribute(n, "token");
                if (!named(n, ptz, "PresetTour") || token.empty())
                    continue;
                const auto title = child(n, schema, "Name") ? value(child(n, schema, "Name")) : token;
                auto* duration = child(child(n, schema, "StartingCondition"), schema, "RecurringDuration");
                const auto limit = duration ? seconds(value(duration)) : 0;
                const bool bounded =
                    limit > 0 && limit <= 60 && child(child(n, schema, "TourSpot"), schema, "PresetDetail");
                if (bounded)
                    add("tour_start", token, title)["duration"] = static_cast<int>(std::ceil(limit));
                auto* state = child(child(n, schema, "Status"), schema, "State");
                if (bounded || (state && (value(state) == "Touring" || value(state) == "Paused")))
                    add("tour_stop", token, title);
            }
        });
        if (control.contains("node"))
            optional("PTZ", ptz, "GetNode", parameter("tptz:NodeToken", control.at("node")), [&](xmlNode* root) {
                auto* node = child(root, ptz, "PTZNode");
                if (attribute(node, "token") != control.at("node").get_ref<const std::string&>())
                    return;
                for (auto* n = node->children; n; n = n->next) {
                    if (!named(n, schema, "AuxiliaryCommands"))
                        continue;
                    const auto token = value(n);
                    for (const auto& [name, command] : {std::pair{"tt:Wiper|On", "wiper_on"},
                                                        {"tt:Wiper|Off", "wiper_off"},
                                                        {"tt:IRLamp|On", "light_on"},
                                                        {"tt:IRLamp|Off", "light_off"},
                                                        {"LightOn", "light_on"},
                                                        {"LightOff", "light_off"}})
                        if (token == name)
                            add(command, token);
                }
            });
    }
    optional("DeviceIO", device, "GetRelayOutputs", {},
             [&](xmlNode* root) {
                 for (auto* n = root->children; n; n = n->next) {
                     const auto token = attribute(n, "token");
                     if (named(n, device, "RelayOutputs") && !token.empty()) {
                         add("relay_on", token);
                         add("relay_off", token);
                     }
                 }
             },
             {"http://www.onvif.org/ver10/deviceio/wsdl/GetRelayOutputs", {}});
    optional("DeviceIO", device_io, "GetDigitalInputs", {},
             [&](xmlNode* root) {
                 for (auto* n = root->children; n && inputs.size() < 64; n = n->next)
                     if (const auto token = attribute(n, "token");
                         named(n, device_io, "DigitalInputs") && !token.empty())
                         inputs.push_back({{"token", token}});
             },
             {"http://www.onvif.org/ver10/deviceio/wsdl/GetDigitalInputs", {}});
    return {{"actions", actions}, {"inputs", inputs}};
}

void operate_camera(const CameraNetworks& networks, const Json& control, const Json& credentials,
                    const CameraAction& action)
{
    auto client = control_client(networks, control, credentials);
    client.actuator = true;
    const auto& cmd = action.command;
    const auto& target = action.target;
    if (cmd.starts_with("focus_")) {
        const auto source = source_parameter(control);
        auto stop = [&] {
            client.deadline = Clock::now() + 3s;
            client.call(control.at("Imaging"), imaging, "Stop", source);
        };
        if (cmd == "focus_stop") {
            stop();
            return;
        }
        const auto& mode = action.mode;
        const bool pulse = mode == "Continuous";
        const auto movement = "<timg:Focus><tt:" + mode + ">" +
                              parameter(pulse ? "tt:Speed" : "tt:Distance", Json(action.amount).dump()) +
                              "</tt:" + mode + "></timg:Focus>";
        try {
            client.call(control.at("Imaging"), imaging, "Move", source + movement);
        } catch (const Failure&) {
            if (pulse) {
                try {
                    stop();
                } catch (const Failure&) {
                    throw Failure{502, "camera_ptz_unknown"};
                }
            }
            throw;
        }
        if (pulse) {
            std::this_thread::sleep_for(200ms);
            try {
                stop();
            } catch (const Failure&) {
                throw Failure{502, "camera_ptz_unknown"};
            }
        }
        return;
    }
    if (cmd.starts_with("relay_")) {
        client.call(control.at("DeviceIO"), device, "SetRelayOutputState",
                    parameter("tds:RelayOutputToken", target) +
                        parameter("tds:LogicalState", cmd == "relay_on" ? "active" : "inactive"),
                    {"http://www.onvif.org/ver10/deviceio/wsdl/SetRelayOutputState", {}});
        return;
    }
    auto profile = profile_parameter(control);
    if (cmd == "preset_goto")
        client.call(control.at("PTZ"), ptz, "GotoPreset", profile + parameter("tptz:PresetToken", target));
    else if (cmd == "tour_start" || cmd == "tour_stop")
        client.call(control.at("PTZ"), ptz, "OperatePresetTour",
                    profile + parameter("tptz:PresetTourToken", target) +
                        parameter("tptz:Operation", cmd == "tour_start" ? "Start" : "Stop"));
    else
        client.call(control.at("PTZ"), ptz, "SendAuxiliaryCommand",
                    profile + parameter("tptz:AuxiliaryData", target));
}

void move_onvif(const CameraNetworks& networks, const Json& control, const Json& credentials,
                const std::string& command)
{
    auto client = control_client(networks, control, credentials);
    client.actuator = true;
    auto profile = "<tptz:ProfileToken>" + escape(control.at("profile").get_ref<const std::string&>()) +
                   "</tptz:ProfileToken>";
    auto url = control.at("PTZ").get<std::string>();
    if (command == "stop") {
        const auto& commands = control.at("directions");
        const auto supports = [&](const char* name) {
            return std::ranges::find(commands, Json(name)) != commands.end();
        };
        const bool pan = supports("left") || supports("right") || supports("up") || supports("down");
        const bool zoom = supports("zoom_in") || supports("zoom_out");
        client.call(url, ptz, "Stop",
                    profile + "<tptz:PanTilt>" + (pan ? "true" : "false") + "</tptz:PanTilt><tptz:Zoom>" +
                        (zoom ? "true" : "false") + "</tptz:Zoom>");
        return;
    }
    const bool relative = control.value("relative_move", false);
    const std::string amount = relative ? "0.03" : "0.5";
    std::string axes;
    if (command == "zoom_in" || command == "zoom_out") {
        axes = "<tt:Zoom space=\"" + std::string{relative ? relative_zoom : zoom_velocity_space} + "\" x=\"" +
               (command == "zoom_in" ? amount : "-" + amount) + "\"/>";
    } else {
        auto x = command == "left" ? "-" + amount : command == "right" ? amount : "0";
        auto y = command == "down" ? "-" + amount : command == "up" ? amount : "0";
        axes = "<tt:PanTilt space=\"" + std::string{relative ? relative_pan : velocity_space} + "\" x=\"" + x +
               "\" y=\"" + y + "\"/>";
    }
    if (relative)
        client.call(url, ptz, "RelativeMove", profile + "<tptz:Translation>" + axes + "</tptz:Translation>");
    else
        client.call(url, ptz, "ContinuousMove",
                    profile + "<tptz:Velocity>" + axes + "</tptz:Velocity><tptz:Timeout>PT1S</tptz:Timeout>");
}

namespace
{
std::string reference_headers(xmlNode* reference)
{
    std::string headers;
    auto* parameters = child(reference, wsa, "ReferenceParameters");
    for (auto* n = parameters ? parameters->children : nullptr; n; n = n->next) {
        if (n->type != XML_ELEMENT_NODE)
            continue;
        if (!n->ns || !n->ns->href || std::string_view{reinterpret_cast<const char*>(n->ns->href)} == wsa ||
            std::string_view{reinterpret_cast<const char*>(n->ns->href)} == soap ||
            std::string_view{reinterpret_cast<const char*>(n->ns->href)}.starts_with(
                "http://docs.oasis-open.org/wss/"))
            throw Failure{502, "camera_protocol_error"};
        Doc copy{xmlNewDoc(BAD_CAST "1.0"), xmlFreeDoc};
        auto* item = xmlDocCopyNode(n, copy.get(), 1);
        xmlDocSetRootElement(copy.get(), item);
        xmlReconciliateNs(copy.get(), item);
        auto* addr = xmlSearchNsByHref(copy.get(), item, BAD_CAST wsa.data());
        if (!addr)
            addr = xmlNewNs(item, BAD_CAST wsa.data(), BAD_CAST "bacuriWsa");
        xmlSetNsProp(item, addr, BAD_CAST "IsReferenceParameter", BAD_CAST "true");
        std::unique_ptr<xmlBuffer, decltype(&xmlBufferFree)> buffer{xmlBufferCreate(), xmlBufferFree};
        if (xmlNodeDump(buffer.get(), copy.get(), item, 0, 0) < 0)
            throw Failure{502, "camera_protocol_error"};
        headers += reinterpret_cast<const char*>(xmlBufferContent(buffer.get()));
        if (headers.size() > 8192)
            throw Failure{502, "camera_protocol_error"};
    }
    return headers;
}

struct CameraTime {
    std::int64_t utc, precision;
};
CameraTime camera_time(const std::string& stamp)
{
    Timestamp timestamp;
    if (!ProtoTime::FromString(stamp, &timestamp))
        throw Failure{502, "camera_protocol_error"};
    std::int64_t precision = 1000000;
    auto dot = stamp.find('.');
    if (dot != stamp.npos)
        for (auto index = dot + 1; index < stamp.size() && precision > 1 && g_ascii_isdigit(stamp[index]); ++index)
            precision /= 10;
    return {ProtoTime::TimestampToMicroseconds(timestamp), precision};
}

std::optional<MotionNotice> motion_notice(xmlNode* notification, const Json& control)
{
    auto* topic = child(notification, wsn, "Topic");
    if (!topic)
        return {};
    auto path = value(topic);
    auto colon = path.find(':');
    const bool digital = colon != path.npos && path.substr(colon + 1) == "Device/Trigger/DigitalInput";
    const auto kind = colon == path.npos ? std::string{} : path.substr(colon + 1);
    const bool crossed = kind == "RuleEngine/LineDetector/Crossed",
               inside = kind == "RuleEngine/FieldDetector/ObjectsInside",
               alarm = kind == "VideoSource/MotionAlarm";
    if (colon == path.npos ||
        (!digital && !crossed && !inside && !alarm && kind != "RuleEngine/CellMotionDetector/Motion"))
        return {};
    auto* ns = xmlSearchNs(topic->doc, topic, BAD_CAST path.substr(0, colon).c_str());
    if (!ns || std::string_view{reinterpret_cast<const char*>(ns->href)} != "http://www.onvif.org/ver10/topics")
        return {};
    auto* message = child(child(notification, wsn, "Message"), schema, "Message");
    const auto operation = attribute(message, "PropertyOperation");
    if (operation != "Changed" && !(crossed && operation.empty()) && !(digital && operation == "Initialized"))
        return {};
    Json source = Json::object();
    auto* items = child(message, schema, "Source");
    for (auto* item = items ? items->children : nullptr; item; item = item->next)
        if (named(item, schema, "SimpleItem"))
            source[attribute(item, "Name")] = attribute(item, "Value");
    if (digital) {
        auto token = source.value("InputToken", std::string{});
        auto* data = child(message, schema, "Data");
        for (auto* n = data ? data->children : nullptr; n && !token.empty(); n = n->next) {
            if (!named(n, schema, "SimpleItem") || attribute(n, "Name") != "LogicalState")
                continue;
            const auto state = attribute(n, "Value");
            if (state != "1" && state != "true" && state != "0" && state != "false")
                continue;
            auto t = camera_time(attribute(message, "UtcTime"));
            return MotionNotice{source, t.utc, t.precision, {}, token, state == "1" || state == "true"};
        }
        return {};
    }
    if (!source.contains("VideoSourceConfigurationToken") ||
        source.at("VideoSourceConfigurationToken") != control.at("source"))
        return {};
    auto* data = child(message, schema, "Data");
    Json fields = Json::object();
    bool active = crossed;
    for (auto* item = data ? data->children : nullptr; item; item = item->next) {
        if (!named(item, schema, "SimpleItem"))
            continue;
        const auto name = attribute(item, "Name"), val = attribute(item, "Value");
        if (name.size() > 128 || val.size() > 512 || fields.size() >= 16)
            return {};
        fields[name] = val;
        if (name == "IsMotion" || (alarm && name == "State") || (inside && name == "IsInside"))
            active = val == "true" || val == "1";
    }
    if (active) {
        auto t = camera_time(attribute(message, "UtcTime"));
        return MotionNotice{source, t.utc, t.precision, {}, {}, false, kind, std::move(fields)};
    }
    return {};
}
} // namespace

void read_motion(std::stop_token stop, const CameraNetworks& networks, const Json& control,
                 const Json& credentials, const std::function<void(const MotionNotice&)>& accept)
{
    auto client = control_client(networks, control, credentials);
    auto subscribed = client.call(control.at("Events"), events, "CreatePullPointSubscription",
                                  "<tev:InitialTerminationTime>PT60S</tev:InitialTerminationTime>",
                                  {std::string{events} + "/EventPortType/CreatePullPointSubscriptionRequest", {}});
    auto* reference = child(response(subscribed, events, "CreatePullPointSubscriptionResponse"), events,
                            "SubscriptionReference");
    auto url = value(child(reference, wsa, "Address"));
    auto headers = reference_headers(reference);
    auto epoch = uuid().substr(9);
    auto unsubscribe = [&] {
        try {
            client.deadline = Clock::now() + 3s;
            client.call(url, wsn, "Unsubscribe", {},
                        {"http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeRequest", headers});
        } catch (const Failure& e) {
            std::fprintf(stderr, "ONVIF subscription cleanup %s\n", e.code.c_str());
        }
    };
    try {
        if (control.contains("DeviceIO")) {
            try {
                client.call(
                    url, events, "SetSynchronizationPoint", {},
                    {std::string{events} + "/PullPointSubscription/SetSynchronizationPointRequest", headers});
            } catch (const Failure& error) {
                // Devices without property synchronization still publish subsequent changes.
                if (error.code != "camera_protocol_error")
                    throw;
            }
        }
        while (!stop.stop_requested()) {
            client.deadline = Clock::now() + 4s;
            auto before = g_get_real_time();
            auto doc = client.call(url, events, "PullMessages",
                                   "<tev:Timeout>PT1S</tev:Timeout><tev:MessageLimit>16</tev:MessageLimit>",
                                   {std::string{events} + "/PullPointSubscription/PullMessagesRequest", headers});
            auto after = g_get_real_time();
            auto* body = response(doc, events, "PullMessagesResponse");
            auto current = camera_time(value(child(body, events, "CurrentTime")));
            auto error =
                std::max(std::abs(current.utc - before), std::abs(after - current.utc)) + current.precision;
            for (auto* n = body->children; n; n = n->next) {
                if (!named(n, wsn, "NotificationMessage"))
                    continue;
                std::optional<MotionNotice> notice;
                try {
                    notice = motion_notice(n, control);
                } catch (const Failure&) {
                    continue;
                }
                if (notice) {
                    notice->uncertainty += error;
                    notice->epoch = epoch;
                    accept(*notice);
                }
            }
            auto termination = camera_time(value(child(body, events, "TerminationTime")));
            if (termination.utc - current.utc < 5000000) {
                client.deadline = Clock::now() + 4s;
                client.call(url, wsn, "Renew", "<wsnt:TerminationTime>PT60S</wsnt:TerminationTime>",
                            {"http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/RenewRequest", headers});
            }
            std::this_thread::sleep_for(100ms);
        }
    } catch (...) {
        unsubscribe();
        throw;
    }
    unsubscribe();
}

Json read_camera_ticket(const Key& key, const std::string& site, const std::string& token)
{
    return read_ticket(key, "camera/" + site, token);
}
} // namespace bacuri::vms::control
