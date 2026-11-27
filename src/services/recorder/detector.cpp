#include "archive/manifest_json.hpp"
#include "services/recorder/detector.hpp"
#include "archive/analysis.hpp"
#include "media/live_codec.hpp"
#include "archive/manifest.hpp"
#include "control.grpc.pb.h"
#include "platform/settings.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <grpcpp/grpcpp.h>
#include <array>

namespace bacuri::vms::ingest
{
namespace
{
using namespace std::chrono_literals;
void decode(const std::stop_token& stop, Stream& stream, const DetectorPoints& detectors,
            const archive::Owner& owner, const std::string& point, std::int64_t binding,
            v1::ArchiveCatalog::Stub& catalog)
{
    auto queue = stream.subscribe();
    std::unique_ptr<media::LiveCodec> pipe;
    VideoCodec codec{};
    std::int64_t last{};
    std::string previous_cells;
    std::array<unsigned char, std::size_t{archive::preview_width} * archive::preview_height> previous{};
    std::map<GstClockTime, domain::ProjectedTime> clocks;
    auto observe = [&](std::string_view pixels, bool, GstClockTime pts, const std::string& backend) {
        const auto utc = static_cast<std::int64_t>(pts / GST_USECOND);
        const auto clock = clocks.extract(pts);
        if (utc - last < 250000)
            return;
        decltype(previous) image{};
        if (pixels.size() != image.size())
            return;
        std::memcpy(image.data(), pixels.data(), image.size());
        if (!last || utc - last > 1500000) {
            previous = image;
            previous_cells.clear();
            last = utc;
            return;
        }
        const std::string cells = archive::motion_sample(previous, image, 0).cells;
        previous = image;
        last = utc;
        v1::SubmitObservationRequest q;
        q.set_site_id(owner.site);
        q.set_source_id(owner.node);
        q.set_schema_version(1);
        q.set_type("motion");
        q.set_observation_id(archive::uuid());
        q.mutable_binding()->set_point_id(point);
        q.mutable_binding()->set_revision(binding);
        q.set_source_time_us(utc);
        if (!clock.empty() && clock.mapped().clock_epoch() && clock.mapped().uncertainty()) {
            q.mutable_clock()->set_epoch_id(clock.mapped().clock_epoch()->to_string());
            q.mutable_clock()->set_uncertainty_us(
                static_cast<std::uint64_t>(std::max<std::int64_t>(0, clock.mapped().uncertainty()->count())));
        }
        q.set_attributes_json(archive::Json{
            {"cells", cells},
            {"previous", previous_cells},
            {"topic", "Local/Motion"},
            {"backend", backend}}.dump());
        previous_cells = cells;
        for (int attempt = 0; attempt < 2 && !stop.stop_requested(); ++attempt) {
            grpc::ClientContext ctx;
            ctx.set_deadline(std::chrono::system_clock::now() + 600ms);
            v1::RegistrationResult ack;
            if (catalog.ReportMotion(&ctx, q, &ack).error_code() != grpc::StatusCode::UNAVAILABLE)
                break;
        }
    };
    while (!stop.stop_requested() && detectors.enabled(point)) {
        auto frame = queue->wait_pop(std::chrono::steady_clock::now() + 200ms);
        if (!frame) {
            if (pipe)
                pipe->poll(observe);
            continue;
        }
        if (!pipe || codec != frame->codec) {
            if (!frame->keyframe)
                continue;
            codec = frame->codec;
            pipe = std::make_unique<media::LiveCodec>(codec == VideoCodec::h265 ? "h265" : "h264",
                                                      media::PrivacyMask{domain::ImageRegions{}},
                                                      media::LiveCodec::Format::gray);
            clocks.clear();
            last = 0;
        }
        const auto utc = std::max<std::int64_t>(0, frame->time.utc().time_since_epoch().count());
        const auto pts = static_cast<GstClockTime>(utc) * GST_USECOND;
        clocks.insert_or_assign(pts, frame->time);
        if (clocks.size() > 901)
            clocks.erase(clocks.begin());
        pipe->push({reinterpret_cast<const char*>(frame->access_unit.data()), frame->access_unit.size()},
                   frame->keyframe, pts, observe);
    }
}
} // namespace
void detect_motion(const std::stop_token& stop, Stream& stream, const DetectorPoints& detectors,
                   const archive::Owner& owner, const std::string& point, std::int64_t binding)
{
    auto catalog = v1::ArchiveCatalog::NewStub(grpc::CreateChannel(
        "unix:" + platform::setting("BACURI_CATALOG_SOCKET"), grpc::experimental::LocalCredentials(UDS)));
    std::mutex mutex;
    std::unique_lock lock{mutex};
    std::condition_variable_any timer;
    while (!stop.stop_requested()) {
        try {
            if (detectors.enabled(point))
                decode(stop, stream, detectors, owner, point, binding, *catalog);
        } catch (const std::exception&) {
            std::fputs("Local detector stream unavailable\n", stderr);
        }
        timer.wait_for(lock, stop, 250ms, [] { return false; });
    }
}
} // namespace bacuri::vms::ingest
