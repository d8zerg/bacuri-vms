// Device information, stream profiles and the channel check of a selected camera.
#include "modules/inventory/onvif.hpp"
#include "modules/inventory/onvif/soap.hpp"

namespace bacuri::vms::control
{
using namespace onvif;

Json inspect_onvif(const CameraNetworks& networks, const Key& key, const std::string& site,
                   const std::string& selection_ticket, const CameraCredentials& credentials)
{
    auto selection = read_ticket(key, "probe/" + site, selection_ticket);
    const std::string url = selection.at("url"), ip = selection.at("ip");
    auto uri = https_uri(url);
    Object<GInetAddress> peer{g_inet_address_new_from_string(ip.c_str()), g_object_unref};
    if (!peer || !networks.contains(peer.get()))
        throw Failure{ErrorCode::camera_transport_denied};
    CameraClient client{credentials, ip, g_uri_get_host(uri.get()), Clock::now() + 15s};
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
                auto endpoint = https_uri(address);
                if (!g_ascii_strcasecmp(client.host.c_str(), g_uri_get_host(endpoint.get())))
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
        if (error.error != ErrorCode::camera_protocol_error)
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
        throw Failure{ErrorCode::camera_incompatible_profiles};
    Doc profiles{nullptr, xmlFreeDoc};
    try {
        profiles = service.profiles(client);
    } catch (const Failure& err) {
        if (service.ns != media2 || !legacy_media || err.error != ErrorCode::camera_protocol_error)
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
            throw Failure{ErrorCode::camera_protocol_error};
        const std::string id = reinterpret_cast<const char*>(token.get());
        if (!tokens.insert(id).second || tokens.size() > 16)
            throw Failure{ErrorCode::camera_protocol_error};
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
                    throw Failure{ErrorCode::camera_protocol_error};
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
        throw Failure{denied_transport ? ErrorCode::camera_transport_denied
                                       : ErrorCode::camera_incompatible_profiles};
    Json enrollment = result;
    enrollment["control"] = std::move(control);
    enrollment["profiles"] = std::move(controls);
    result["profiles"] = std::move(options);
    result["selection"] = ticket(key, "camera/" + site, std::move(enrollment));
    if (result.at("selection").get_ref<const std::string&>().size() > 24576)
        throw Failure{ErrorCode::camera_protocol_error};
    return result;
}

void verify_camera_channel(const CameraNetworks& networks, const CameraChannel& config)
{
    const auto& control = config.control;
    auto client = control_client(networks, control, config.credentials);
    auto info = client.call(required_setting(control.device_service), device, "GetDeviceInformation");
    auto* identity = response(info, device, "GetDeviceInformationResponse");
    if (value(child(identity, device, "SerialNumber")) != config.serial ||
        value(child(identity, device, "Model")) != config.model ||
        value(child(identity, device, "FirmwareVersion")) != config.firmware)
        throw Failure{ErrorCode::camera_binding_conflict};
    MediaService service{required_setting(control.media_service), control.media2 ? media2 : media};
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
        if ((token ? value(token) : attribute(source, "token")) == required_setting(control.channel))
            compatible.insert(attribute(p, "token"));
    }
    const auto verify_stream = [&](const std::string& profile, const std::string& url) {
        if (!compatible.contains(profile))
            throw Failure{ErrorCode::camera_binding_conflict};
        const auto uri = service.stream(client, profile);
        if (uri != url || !networks.permits(uri))
            throw Failure{ErrorCode::camera_binding_conflict};
    };
    verify_stream(required_setting(control.profile), config.main_url);
    verify_stream(required_setting(control.secondary_profile), config.secondary_url);
}

Json read_camera_ticket(const Key& key, const std::string& site, const std::string& token)
{
    return read_ticket(key, "camera/" + site, token);
}
} // namespace bacuri::vms::control
