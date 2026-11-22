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
constexpr std::string_view media = "http://www.onvif.org/ver10/media/wsdl";
constexpr std::string_view schema = "http://www.onvif.org/ver10/schema";
constexpr std::string_view ptz = "http://www.onvif.org/ver20/ptz/wsdl";
constexpr std::string_view events = "http://www.onvif.org/ver10/events/wsdl";
constexpr std::string_view wsn = "http://docs.oasis-open.org/wsn/b-2";
constexpr std::string_view wsa = "http://www.w3.org/2005/08/addressing";
constexpr std::string_view velocity_space = "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace";
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
    return R"(<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:a="http://www.w3.org/2005/08/addressing" xmlns:w="http://schemas.xmlsoap.org/ws/2004/08/addressing" xmlns:d="http://schemas.xmlsoap.org/ws/2005/04/discovery" xmlns:dn="http://www.onvif.org/ver10/network/wsdl" xmlns:tds="http://www.onvif.org/ver10/device/wsdl" xmlns:trt="http://www.onvif.org/ver10/media/wsdl" xmlns:tptz="http://www.onvif.org/ver20/ptz/wsdl" xmlns:tev="http://www.onvif.org/ver10/events/wsdl" xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2" xmlns:tt="http://www.onvif.org/ver10/schema"><s:Header>)" +
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
            throw Failure{502, "camera_unavailable"};
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
        }
        if (!g_tls_connection_handshake(G_TLS_CONNECTION(tls.get()), nullptr, nullptr))
            throw Failure{502, "camera_tls_error"};
        const std::string action = headers.action.empty() ? std::string{ns} + "/" + operation : headers.action;
        const std::string element = (ns == device   ? "tds:"
                                     : ns == media  ? "trt:"
                                     : ns == ptz    ? "tptz:"
                                     : ns == events ? "tev:"
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
    if (!timeout)
        return result;
    auto min = seconds(value(child(timeout, schema, "Min")));
    auto max = seconds(value(child(timeout, schema, "Max")));
    if (!(min >= 0 && min <= 1 && max >= 1))
        return result;
    auto* spaces = child(opts, schema, "Spaces");
    for (auto* n = spaces ? spaces->children : nullptr; n; n = n->next) {
        if (!named(n, schema, "ContinuousPanTiltVelocitySpace") ||
            value(child(n, schema, "URI")) != velocity_space)
            continue;
        auto range = [&](const char* axis, const char* bound) {
            auto raw = value(child(child(n, schema, axis), schema, bound));
            char* end{};
            auto number = g_ascii_strtod(raw.c_str(), &end);
            if (*end || !std::isfinite(number))
                throw Failure{502, "camera_protocol_error"};
            return number;
        };
        auto xmin = range("XRange", "Min"), xmax = range("XRange", "Max"), ymin = range("YRange", "Min"),
             ymax = range("YRange", "Max");
        if (xmin <= 0 && xmax >= 0 && ymin <= 0 && ymax >= 0) {
            if (xmin <= -0.5)
                result.push_back("left");
            if (xmax >= 0.5)
                result.push_back("right");
            if (ymin <= -0.5)
                result.push_back("down");
            if (ymax >= 0.5)
                result.push_back("up");
        }
        break;
    }
    if (!result.empty())
        result.push_back("stop");
    return result;
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
        for (std::string url; urls >> url;) {
            if (!url.starts_with("https://"))
                continue;
            auto uri = https_uri(url);
            if (!seen.insert(id).second)
                break;
            found.push_back(
                {{"endpoint_id", id},
                 {"device_service", url},
                 {"selection", ticket(key, "probe/" + site, {{"id", id}, {"url", url}, {"ip", ip.get()}})}});
            break;
        }
    }
}
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
    auto service = value(child(child(services, schema, "Media"), schema, "XAddr"));
    result["ptz"] = child(services, schema, "PTZ") != nullptr;
    result["events"] = child(services, schema, "Events") != nullptr;
    auto profiles = client.call(service, media, "GetProfiles");
    Json options = Json::array();
    Json control{{"ip", ip}, {"host", client.host}};
    for (const auto* name : {"PTZ", "Events"}) {
        if (auto* address = child(child(services, schema, name), schema, "XAddr"))
            control[name] = value(address);
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
    Json controls = Json::object();
    std::map<std::string, Json> ptz_options;
    std::set<std::string> tokens, addresses;
    bool denied_transport = false;
    for (auto* p = response(profiles, media, "GetProfilesResponse")->children; p; p = p->next) {
        if (!named(p, media, "Profiles"))
            continue;
        auto* enc = child(p, schema, "VideoEncoderConfiguration");
        if (!enc || value(child(enc, schema, "Encoding")) != "H264")
            continue;
        std::unique_ptr<xmlChar, xmlFreeFunc> token{xmlGetProp(p, BAD_CAST "token"), xmlFree};
        if (!token || xmlStrlen(token.get()) > 128 || !*token)
            throw Failure{502, "camera_protocol_error"};
        const std::string id = reinterpret_cast<const char*>(token.get());
        if (!tokens.insert(id).second || tokens.size() > 16)
            throw Failure{502, "camera_protocol_error"};
        auto stream =
            client.call(service, media, "GetStreamUri",
                        "<trt:StreamSetup><tt:Stream>RTP-Unicast</tt:Stream><tt:Transport><tt:Protocol>RTSP</"
                        "tt:Protocol></tt:Transport></trt:StreamSetup><trt:ProfileToken>" +
                            escape(id) + "</trt:ProfileToken>");
        auto address =
            value(child(child(response(stream, media, "GetStreamUriResponse"), media, "MediaUri"), schema, "Uri"));
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
        auto* source = child(p, schema, "VideoSourceConfiguration");
        selected["source"] = attribute(source, "token");
        auto config = attribute(child(p, schema, "PTZConfiguration"), "token");
        selected["ptz_profile"] = !config.empty();
        if (!config.empty() && control.contains("PTZ")) {
            auto* cfg = child(p, schema, "PTZConfiguration");
            for (auto [field, element] : {std::pair{"pan_space", "DefaultAbsolutePantTiltPositionSpace"},
                                          std::pair{"zoom_space", "DefaultAbsoluteZoomPositionSpace"}})
                if (auto* node = child(cfg, schema, element))
                    selected[field] = value(node);
            if (!ptz_options.contains(config)) {
                try {
                    ptz_options[config] = directions(client, control.at("PTZ"), config);
                } catch (const Failure&) {
                    ptz_options[config] = Json::array();
                }
            }
            selected["directions"] = ptz_options.at(config);
        }
        controls[fingerprint(key, "onvif-stream", address)] = std::move(selected);
        options.push_back({{"token", id}, {"name", value(child(p, schema, "Name"))}, {"url", address}});
    }
    if (options.size() < 2)
        throw Failure{422, denied_transport ? "camera_transport_denied" : "camera_incompatible_profiles"};
    Json enrollment = result;
    enrollment["control"] = std::move(control);
    enrollment["profiles"] = std::move(controls);
    for (const auto& option : options)
        enrollment["stream_ids"].push_back(
            fingerprint(key, "onvif-stream", option.at("url").get_ref<const std::string&>()));
    result["profiles"] = std::move(options);
    result["selection"] = ticket(key, "camera/" + site, std::move(enrollment));
    if (result.at("selection").get_ref<const std::string&>().size() > 24576)
        throw Failure{502, "camera_protocol_error"};
    return result;
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
    if (auto* motion = child(child(status, schema, "MoveStatus"), schema, "PanTilt"))
        sample["moving"] = value(motion) == "MOVING";
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

void move_onvif(const CameraNetworks& networks, const Json& control, const Json& credentials,
                const std::string& command)
{
    auto client = control_client(networks, control, credentials);
    auto profile = "<tptz:ProfileToken>" + escape(control.at("profile").get_ref<const std::string&>()) +
                   "</tptz:ProfileToken>";
    auto url = control.at("PTZ").get<std::string>();
    if (command == "stop") {
        client.call(url, ptz, "Stop", profile + "<tptz:PanTilt>true</tptz:PanTilt><tptz:Zoom>false</tptz:Zoom>");
    } else {
        auto x = command == "left" ? "-0.5" : command == "right" ? "0.5" : "0";
        auto y = command == "down" ? "-0.5" : command == "up" ? "0.5" : "0";
        client.call(url, ptz, "ContinuousMove",
                    profile + "<tptz:Velocity><tt:PanTilt space=\"" + std::string{velocity_space} + "\" x=\"" + x +
                        "\" y=\"" + y + "\"/></tptz:Velocity><tptz:Timeout>PT1S</tptz:Timeout>");
    }
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
    if (colon == path.npos || path.substr(colon + 1) != "RuleEngine/CellMotionDetector/Motion")
        return {};
    auto* ns = xmlSearchNs(topic->doc, topic, BAD_CAST path.substr(0, colon).c_str());
    if (!ns || std::string_view{reinterpret_cast<const char*>(ns->href)} != "http://www.onvif.org/ver10/topics")
        return {};
    auto* message = child(child(notification, wsn, "Message"), schema, "Message");
    if (attribute(message, "PropertyOperation") != "Changed")
        return {};
    Json source = Json::object();
    auto* items = child(message, schema, "Source");
    for (auto* item = items ? items->children : nullptr; item; item = item->next)
        if (named(item, schema, "SimpleItem"))
            source[attribute(item, "Name")] = attribute(item, "Value");
    if (!source.contains("VideoSourceConfigurationToken") ||
        source.at("VideoSourceConfigurationToken") != control.at("source"))
        return {};
    auto* data = child(message, schema, "Data");
    for (auto* item = data ? data->children : nullptr; item; item = item->next) {
        if (!named(item, schema, "SimpleItem") || attribute(item, "Name") != "IsMotion")
            continue;
        auto state = attribute(item, "Value");
        if (state == "true" || state == "1") {
            auto t = camera_time(attribute(message, "UtcTime"));
            return MotionNotice{source, t.utc, t.precision, {}};
        }
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
