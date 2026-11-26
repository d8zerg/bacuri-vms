#include <nlohmann/json.hpp>
#include "maintenance.hpp"
#include "archive/volume.hpp"
#include "platform/identifiers.hpp"
#include "platform/settings.hpp"
#include <sodium.h>
#include <chrono>
#include <sstream>
#include <syslog.h>
namespace bacuri::vms::maintenance
{
namespace
{
using Json = nlohmann::json;
using Clock = std::chrono::system_clock;
std::string now()
{
    return std::to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
}
template <std::size_t N> std::string id()
{
    std::array<unsigned char, N> bytes{};
    std::array<char, N * 2 + 1> hex{};
    randombytes_buf(bytes.data(), N);
    sodium_bin2hex(hex.data(), hex.size(), bytes.data(), bytes.size());
    return hex.data();
}
struct Sample {
    std::string trace = id<16>(), root = id<8>(), start = now(), metrics, logs;
    Json spans = Json::array();
    template <class Probe> void observe(const std::string& name, Probe probe)
    {
        auto span = id<8>(), begin = now();
        Json values = Json::object();
        bool ok = true;
        try {
            probe(values);
        } catch (const std::exception&) {
            ok = false;
        }
        Json attrs = Json::array();
        for (const auto& [key, v] : values.items())
            attrs.push_back({{"key", key}, {"value", {{"intValue", v.dump()}}}});
        spans.push_back({{"traceId", trace},
                         {"spanId", span},
                         {"parentSpanId", root},
                         {"name", name},
                         {"kind", 1},
                         {"startTimeUnixNano", begin},
                         {"endTimeUnixNano", now()},
                         {"attributes", attrs},
                         {"status", {{"code", ok ? 1 : 2}}}});
        const auto line =
            Json{{"event", name}, {"trace_id", trace}, {"span_id", span}, {"ok", ok}, {"values", values}}.dump();
        logs += line + "\n";
        syslog(LOG_INFO, "%s", line.c_str());
        metrics += "bacuri_diagnostic_probe_up{probe=\"" + name + "\"} " + (ok ? "1\n" : "0\n");
    }
};
} // namespace
void diagnose(const fs::path& dest)
{
    ensure(fs::create_directory(dest), "Choose a new diagnostic directory");
    Sample s;
    std::string invocation;
    s.observe("systemd.services", [&](Json& fields) {
        for (const std::string unit : {"core", "recorder", "media"}) {
            const auto active = capture({"/usr/bin/systemctl", "show", "-p", "ActiveState", "--value",
                                         "bacuri-" + unit + ".service"}) == "active"
                                    ? 1
                                    : 0;
            fields[unit] = active;
            s.metrics += "bacuri_service_up{service=\"" + unit + "\"} " + std::to_string(active) + "\n";
        }
        invocation =
            capture({"/usr/bin/systemctl", "show", "-p", "InvocationID", "--value", "bacuri-recorder.service"});
    });
    s.observe("recorder.sources", [&](Json& fields) {
        ensure(invocation.size() == 32, "Recorder inactive");
        std::istringstream lines{capture({"/usr/bin/journalctl", "--no-pager", "--since", "2 minutes ago", "-n",
                                          "4096", "-o", "cat", "_SYSTEMD_INVOCATION_ID=" + invocation})};
        std::map<std::string, int> sources;
        for (std::string line; std::getline(lines, line);) {
            auto entry = Json::parse(line, nullptr, false);
            if (!entry.is_object() || entry.value("event", "") != "rtsp_ingest" ||
                entry.value("profile", "") != "main")
                continue;
            auto device = entry.value("device", "");
            if (platform::canonical_id(device))
                sources[device] = entry.value("state", "") == "receiving" ? 1 : 0;
        }
        for (const auto& [device, up] : sources) {
            fields[device] = up;
            s.metrics += "bacuri_camera_up{device=\"" + device + "\"} " + std::to_string(up) + "\n";
        }
    });
    s.observe("archive.catalog", [&](Json& fields) {
        auto v = archive::archive_status("/srv/bacuri-archive/recorder",
                                         platform::optional_setting("BACURI_ARCHIVE_REPLICA_DIR"));
        fields["pending"] = v.pending;
        fields["available_bytes"] = v.available_bytes;
        s.metrics += "bacuri_catalog_pending_blocks " + std::to_string(v.pending) + "\n";
        s.metrics += "bacuri_archive_available_bytes " + std::to_string(v.available_bytes) + "\n";
        int stopped = v.state == archive::CapacityState::recording_stopped ? 1 : 0;
        fields["recording_stopped"] = stopped;
        s.metrics += "bacuri_archive_recording_stopped " + std::to_string(stopped) + "\n";
    });
    s.observe("services.metrics", [&](Json& fields) {
        // Each service replaces its exposition every few seconds. A stale file means the service stopped
        // exporting, and its metrics are left out instead of being reported as current.
        constexpr auto fresh = std::chrono::minutes{1};
        for (const std::string role : {"core", "recorder", "media"}) {
            const fs::path file = fs::path{"/run/bacuri-vms"} / role / (role + ".prom");
            std::error_code error;
            const auto changed = fs::last_write_time(file, error);
            const bool current = !error && fs::file_time_type::clock::now() - changed < fresh;
            fields[role] = current ? 1 : 0;
            if (current)
                s.metrics += read(file);
        }
    });
    s.spans.push_back({{"traceId", s.trace},
                       {"spanId", s.root},
                       {"name", "bacuri.diagnose"},
                       {"kind", 1},
                       {"startTimeUnixNano", s.start},
                       {"endTimeUnixNano", now()}});
    Json scope{{"scope", {{"name", "bacuri.vms.operations"}}}, {"spans", s.spans}};
    Json resource{{"attributes",
                   Json::array({Json{{"key", "service.name"}, {"value", {{"stringValue", "bacuri-maintain"}}}}})}};
    Json payload{
        {"resourceSpans", Json::array({Json{{"resource", resource}, {"scopeSpans", Json::array({scope})}}})}};
    put(dest / "metrics.prom", s.metrics);
    put(dest / "events.jsonl", s.logs);
    put(dest / "traces.json", payload.dump() + "\n");
}
} // namespace bacuri::vms::maintenance
