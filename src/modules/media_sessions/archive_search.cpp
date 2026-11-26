#include "modules/media_sessions/playback_operations.hpp"
#include "application/control_api/playback_time.hpp"
#include "modules/access/operations.hpp"
#include "modules/recordings/operations.hpp"
#include "media/lease.hpp"

namespace bacuri::vms::control::playback
{
using namespace std::chrono_literals;
Json search(postgresql::Database& db, const Key& key, const PointAccess& access, Json params,
            std::optional<std::int64_t> binding)
{
    const auto cursor = params.value("cursor", std::string{});
    const bool continued = params.erase("cursor") != 0;
    const auto presentation = params.value("presentation", "");
    if (!presentation.empty() && presentation != "masked")
        throw Failure{ErrorCode::invalid_request};
    params.erase("presentation");
    const auto bounds = window(params);
    const auto mask =
        presentation == "masked" ? access::delivery_regions(db, access.site, access.point, "archive") : "";
    if (!access::allowed(db, access.site, access.user, access.point, "view", mask.empty() ? "archive" : "",
                         std::to_string(bounds.start), std::to_string(bounds.end)))
        throw Failure{ErrorCode::not_found};
    recordings::require_available(db, access.site);
    auto position = bounds.start;
    const auto context =
        Json::array({"archive", access.browser, access.role, access.site, access.point, bounds.start, bounds.end})
            .dump();
    if (continued) {
        try {
            if (cursor.empty() || cursor.size() > 1024) {
                throw std::invalid_argument("Invalid cursor");
            }
            auto plain = unseal("k1:" + cursor, key, context);
            position = std::stoll(plain);
            if (position <= bounds.start || position >= bounds.end) {
                throw std::invalid_argument("Invalid cursor");
            }
        } catch (const std::exception&) {
            throw Failure{ErrorCode::invalid_request};
        }
    }
    auto recorder = v1::RecorderControl::NewStub(media::local_channel("BACURI_SOCKET"));
    grpc::ClientContext rpc;
    rpc.set_deadline(std::chrono::system_clock::now() + 1s);
    v1::PendingQuery q;
    q.mutable_point()->set_site_id(access.site);
    q.mutable_point()->set_point_id(access.point);
    q.set_begin_us(position);
    q.set_end_us(bounds.end);
    q.set_retention_cutoff_us(recordings::retention_cutoff(db, access.site, access.point));
    if (binding) {
        q.set_binding_revision(*binding);
    }
    v1::PendingIntervals pending;
    Json local = Json::array();
    auto horizon = bounds.end;
    if (recorder->ListPending(&rpc, q, &pending).ok()) {
        for (const auto& interval : pending.intervals()) {
            if (local.size() == 1000 && interval.begin_us() > position) {
                horizon = interval.begin_us();
                break;
            }
            local.push_back({{"a", interval.begin_us()}, {"b", interval.end_us()}});
        }
    }
    auto rows =
        recordings::coverage(db, access.site, access.point, std::to_string(position), std::to_string(horizon),
                             local.dump(), binding ? std::to_string(*binding) : "");
    auto items = Json::array();
    for (const auto& row : rows) {
        if (items.size() == 1000)
            break;
        Window span{row.begin, row.end};
        Json entry{{"interval", span.json()}, {"state", row.state}};
        if (auto basis = row.time_basis; !basis.empty()) {
            entry["time_basis"] = basis;
        }
        if (auto uncertainty = row.uncertainty; !uncertainty.empty()) {
            entry["uncertainty_us"] = uncertainty;
        }
        if (auto reason = row.reason; !reason.empty()) {
            entry["reason"] = reason;
        }
        items.push_back(std::move(entry));
        position = span.end;
    }
    Json page{{"items", std::move(items)}};
    if (position < bounds.end) {
        page["next_cursor"] = seal(key, context, std::to_string(position)).substr(3);
    }
    return page;
}
} // namespace bacuri::vms::control::playback
