// Device information, stream profiles and the channel check of a selected camera.
#include "modules/inventory/onvif.hpp"
#include "modules/inventory/onvif/soap.hpp"
#include "modules/inventory/onvif/ticket_json.hpp"

namespace bacuri::vms::control
{
using namespace onvif;

InspectedCamera inspect_onvif(const CameraNetworks& networks, const Key& key, const std::string& site,
                              const std::string& selection_ticket, const CameraCredentials& credentials)
{
    auto selection = read_ticket<ProbeCandidate>(key, site, selection_ticket);
    const std::string url = selection.url, ip = selection.ip;
    auto uri = https_uri(url);
    Object<GInetAddress> peer{g_inet_address_new_from_string(ip.c_str()), g_object_unref};
    if (!peer || !networks.contains(peer.get()))
        throw Failure{ErrorCode::camera_transport_denied};
    CameraClient client{credentials, ip, g_uri_get_host(uri.get()), Clock::now() + 15s};
    auto info = client.call(url, device, "GetDeviceInformation");
    auto* identity = response(info, device, "GetDeviceInformationResponse");
    CameraSelection enrollment{};
    auto& camera = enrollment.identity;
    camera.endpoint_id = selection.id;
    camera.model = value(child(identity, device, "Model"));
    camera.manufacturer = value(child(identity, device, "Manufacturer"));
    camera.firmware = value(child(identity, device, "FirmwareVersion"));
    camera.serial = value(child(identity, device, "SerialNumber"));
    auto caps = client.call(url, device, "GetCapabilities", "<tds:Category>All</tds:Category>");
    auto* services = child(response(caps, device, "GetCapabilitiesResponse"), device, "Capabilities");
    auto* legacy_media = child(child(services, schema, "Media"), schema, "XAddr");
    MediaService service{legacy_media ? value(legacy_media) : "", media};
    camera.ptz = child(services, schema, "PTZ") != nullptr;
    camera.events = child(services, schema, "Events") != nullptr;
    std::vector<CameraProfile> options;
    auto& control = enrollment.services;
    control.ip = ip;
    control.host = client.host;
    control.device_service = url;
    for (auto [name, field] : {std::pair{"PTZ", &OnvifServices::ptz},
                               {"Events", &OnvifServices::events},
                               {"Imaging", &OnvifServices::imaging}}) {
        if (auto* address = child(child(services, schema, name), schema, "XAddr"))
            control.*field = value(address);
    }
    if (auto* address = child(child(child(services, schema, "Extension"), schema, "DeviceIO"), schema, "XAddr"))
        control.device_io = value(address);
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
            for (auto [space, field] : {std::pair{events, &OnvifServices::events},
                                        {imaging, &OnvifServices::imaging},
                                        {ptz, &OnvifServices::ptz},
                                        {device_io, &OnvifServices::device_io}}) {
                if (space != ns || !address.starts_with("https://"))
                    continue;
                auto endpoint = https_uri(address);
                if (!g_ascii_strcasecmp(client.host.c_str(), g_uri_get_host(endpoint.get())))
                    control.*field = address;
            }
        }
    } catch (const Failure& error) {
        // Earlier devices only implement GetCapabilities.
        if (error.error != ErrorCode::camera_protocol_error)
            throw;
    }
    if (control.ptz) {
        try {
            auto reply = client.call(*control.ptz, ptz, "GetServiceCapabilities");
            auto* capabilities =
                child(response(reply, ptz, "GetServiceCapabilitiesResponse"), ptz, "Capabilities");
            const auto flag = attribute(capabilities, "StatusPosition");
            if (flag == "true" || flag == "1" || flag == "false" || flag == "0")
                control.status_position = flag == "true" || flag == "1";
        } catch (const Failure&) {
            // Older services omit capability discovery. GetStatus can still report a position.
            control.status_position.reset();
        }
    }
    if (auto* supported = child(child(services, schema, "Events"), schema, "WSPullPointSupport")) {
        auto flag = value(supported);
        if (flag != "true" && flag != "1")
            control.events.reset();
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
    control.media_service = service.address;
    if (service.ns == media2)
        control.media2 = true;
    std::map<std::string, std::optional<MoveOptions>> ptz_options;
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
        SelectedProfile selected{};
        selected.profile = id;
        selected.directions.emplace();
        selected.audio_input = service.configuration(p, "AudioSourceConfiguration", "AudioSource") != nullptr;
        auto* io = child(child(services, schema, "Extension"), schema, "DeviceIO");
        selected.audio_output =
            audio_output || service.configuration(p, "AudioOutputConfiguration", "AudioOutput") ||
            child(child(p, schema, "Extension"), schema, "AudioOutputConfiguration") ||
            (child(io, schema, "AudioOutputs") &&
             g_ascii_strtoull(value(child(io, schema, "AudioOutputs")).c_str(), nullptr, 10) > 0);
        auto* source = service.configuration(p, "VideoSourceConfiguration", "VideoSource");
        selected.source = attribute(source, "token");
        auto* physical_source = child(source, schema, "SourceToken");
        selected.channel = physical_source ? value(physical_source) : *selected.source;
        selected.imaging_source = physical_source != nullptr;
        auto config = attribute(service.configuration(p, "PTZConfiguration", "PTZ"), "token");
        selected.ptz_profile = !config.empty();
        if (!config.empty() && control.ptz) {
            auto* cfg = service.configuration(p, "PTZConfiguration", "PTZ");
            if (auto* node = child(cfg, schema, "NodeToken"))
                selected.node = value(node);
            if (auto* node = child(cfg, schema, "DefaultAbsolutePantTiltPositionSpace"))
                selected.pan_space = value(node);
            if (auto* node = child(cfg, schema, "DefaultAbsoluteZoomPositionSpace"))
                selected.zoom_space = value(node);
            auto [options_it, inserted] = ptz_options.try_emplace(config);
            if (inserted) {
                try {
                    options_it->second = directions(client, *control.ptz, config);
                } catch (const Failure&) {
                    // Cache unavailable move options for other streams using the same PTZ profile.
                    options_it->second.reset();
                }
            }
            if (const auto& move = options_it->second) {
                selected.directions = move->directions;
                selected.relative_move = move->relative;
            }
        }
        CameraProfile option{id,
                             value(child(p, service.ns == media2 ? media2 : schema, "Name")),
                             address,
                             *selected.channel,
                             encoding == "H264" ? "h264" : "h265",
                             {},
                             {}};
        if (auto* resolution = child(enc, schema, "Resolution")) {
            auto dimension = [&](const char* name) {
                const auto s = value(child(resolution, schema, name));
                char* end = nullptr;
                auto n = g_ascii_strtoull(s.c_str(), &end, 10);
                if (*end || n == 0 || n > 65535)
                    throw Failure{ErrorCode::camera_protocol_error};
                return n;
            };
            option.width = static_cast<unsigned>(dimension("Width"));
            option.height = static_cast<unsigned>(dimension("Height"));
        }
        selected.stream_url = address;
        selected.codec = option.codec;
        selected.pixels = std::int64_t{option.width.value_or(0)} * option.height.value_or(0);
        enrollment.profiles[fingerprint(key, "onvif-stream", address)] = std::move(selected);
        options.push_back(std::move(option));
    }
    if (options.size() < 2)
        throw Failure{denied_transport ? ErrorCode::camera_transport_denied
                                       : ErrorCode::camera_incompatible_profiles};
    auto selection_ticket_out = issue_ticket(key, site, enrollment);
    if (selection_ticket_out.size() > 24576)
        throw Failure{ErrorCode::camera_protocol_error};
    return {std::move(camera.endpoint_id),
            std::move(camera.manufacturer),
            std::move(camera.model),
            std::move(camera.firmware),
            std::move(camera.serial),
            std::move(selection_ticket_out),
            camera.ptz,
            camera.events,
            std::move(options)};
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

CameraSelection read_camera_ticket(const Key& key, const std::string& site, const std::string& token)
{
    return read_ticket<CameraSelection>(key, site, token);
}
} // namespace bacuri::vms::control
