#include "application/control_api/http_json.hpp"
#include "modules/media_sessions/playback_operations.hpp"
#include "application/control_api/playback_time.hpp"
#include "modules/access/operations.hpp"
#include "modules/recordings/operations.hpp"
#include "media/lease.hpp"

namespace bacuri::vms::control::playback
{
using namespace std::chrono_literals;
ArchivePage search(postgresql::Database& db, const Key& key, const PointAccess& access, const SearchQuery& query,
                   std::optional<std::int64_t> binding)
{
    const auto bounds = query.interval;
    const auto mask =
        query.masked ? access::delivery_regions(db, access.site, access.point, "archive") : domain::ImageRegions{};
    if (!access::allowed(db, access.site, access.user, access.point, "view", mask.empty() ? "archive" : "",
                         std::to_string(bounds.start), std::to_string(bounds.end)))
        throw Failure{ErrorCode::not_found};
    recordings::require_available(db, access.site);
    auto position = bounds.start;
    const auto context =
        Json::array({"archive", access.browser, access.role, access.site, access.point, bounds.start, bounds.end})
            .dump();
    if (query.cursor) {
        const auto& cursor = *query.cursor;
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
    std::vector<recordings::PendingInterval> local;
    auto horizon = bounds.end;
    if (recorder->ListPending(&rpc, q, &pending).ok()) {
        for (const auto& interval : pending.intervals()) {
            if (local.size() == 1000 && interval.begin_us() > position) {
                horizon = interval.begin_us();
                break;
            }
            local.push_back({interval.begin_us(), interval.end_us()});
        }
    }
    auto rows = recordings::coverage(db, access.site, access.point, std::to_string(position),
                                     std::to_string(horizon), local, binding ? std::to_string(*binding) : "");
    if (rows.size() > 1000)
        rows.resize(1000);
    if (!rows.empty())
        position = rows.back().end;
    ArchivePage page{std::move(rows), {}};
    if (position < bounds.end)
        page.next_cursor = seal(key, context, std::to_string(position)).substr(3);
    return page;
}
} // namespace bacuri::vms::control::playback
