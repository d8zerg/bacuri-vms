// Motion and input events of a camera through a pull-point subscription.
#include "modules/inventory/onvif.hpp"
#include "modules/inventory/onvif/soap.hpp"

namespace bacuri::vms::control
{
using namespace onvif;

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
            throw Failure{ErrorCode::camera_protocol_error};
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
            throw Failure{ErrorCode::camera_protocol_error};
        headers += reinterpret_cast<const char*>(xmlBufferContent(buffer.get()));
        if (headers.size() > 8192)
            throw Failure{ErrorCode::camera_protocol_error};
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
        throw Failure{ErrorCode::camera_protocol_error};
    std::int64_t precision = 1000000;
    auto dot = stamp.find('.');
    if (dot != stamp.npos)
        for (auto index = dot + 1; index < stamp.size() && precision > 1 && g_ascii_isdigit(stamp[index]); ++index)
            precision /= 10;
    return {ProtoTime::TimestampToMicroseconds(timestamp), precision};
}

std::optional<MotionNotice> motion_notice(xmlNode* notification, const CameraControl& control)
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
        source.at("VideoSourceConfigurationToken") != required_setting(control.source))
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

void read_motion(const std::stop_token& stop, const CameraNetworks& networks, const CameraControl& control,
                 const CameraCredentials& credentials, const std::function<void(const MotionNotice&)>& accept)
{
    auto client = control_client(networks, control, credentials);
    auto subscribed = client.call(required_setting(control.events), events, "CreatePullPointSubscription",
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
            std::fprintf(stderr, "ONVIF subscription cleanup %s\n", std::string{e.code()}.c_str());
        }
    };
    try {
        if (control.device_io.has_value()) {
            try {
                client.call(
                    url, events, "SetSynchronizationPoint", {},
                    {std::string{events} + "/PullPointSubscription/SetSynchronizationPointRequest", headers});
            } catch (const Failure& error) {
                // Devices without property synchronization still publish subsequent changes.
                if (error.error != ErrorCode::camera_protocol_error)
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
} // namespace bacuri::vms::control
