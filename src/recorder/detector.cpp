#include "recorder/detector.hpp"
#include "archive/analysis.hpp"
#include "control.grpc.pb.h"
#include "control/support.hpp"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <grpcpp/grpcpp.h>
#include <array>

namespace bacuri::vms::ingest
{
namespace
{
using namespace std::chrono_literals;
struct Pipeline {
    GstElement* pipe{};
    GstElement* input{};
    GstElement* output{};
    ~Pipeline()
    {
        if (pipe)
            gst_element_set_state(pipe, GST_STATE_NULL);
        if (input)
            gst_object_unref(input);
        if (output)
            gst_object_unref(output);
        if (pipe)
            gst_object_unref(pipe);
    }
};
void decode(std::stop_token stop, Stream& stream, archive::Volume& volume, const std::string& point,
            std::int64_t binding, v1::ArchiveCatalog::Stub& catalog)
{
    auto queue = stream.subscribe();
    Pipeline p;
    VideoCodec codec{};
    std::int64_t last{};
    std::string previous_cells;
    std::array<unsigned char, std::size_t{archive::preview_width} * archive::preview_height> previous{};
    while (!stop.stop_requested() && volume.detector_enabled(point)) {
        const auto frame = queue->wait_pop(std::chrono::steady_clock::now() + 200ms);
        if (!frame)
            continue;
        if (!p.pipe) {
            if (!frame->keyframe)
                continue;
            gst_init(nullptr, nullptr);
            codec = frame->codec;
            const auto type = codec == VideoCodec::h265 ? "h265" : "h264";
            const auto chain =
                std::string{"appsrc name=input is-live=true format=time max-bytes=8388608 ! video/x-"} + type +
                ",stream-format=byte-stream,alignment=au ! " + type +
                "parse ! decodebin ! videoconvert ! videoscale ! video/x-raw,format=GRAY8,width=64,height=48 ! "
                "appsink name=output sync=false max-buffers=1 drop=true wait-on-eos=false";
            GError* error{};
            p.pipe = gst_parse_launch(chain.c_str(), &error);
            if (error) {
                g_error_free(error);
                return;
            }
            if (!p.pipe)
                return;
            p.input = gst_bin_get_by_name(GST_BIN(p.pipe), "input");
            p.output = gst_bin_get_by_name(GST_BIN(p.pipe), "output");
            if (gst_element_set_state(p.pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
                return;
        }
        if (codec != frame->codec ||
            gst_app_src_get_current_level_bytes(GST_APP_SRC(p.input)) > 8ULL * 1024 * 1024)
            return;
        auto* buffer = gst_buffer_new_allocate(nullptr, frame->access_unit.size(), nullptr);
        gst_buffer_fill(buffer, 0, frame->access_unit.data(), frame->access_unit.size());
        GST_BUFFER_PTS(buffer) = frame->time.utc().time_since_epoch().count() * GST_USECOND;
        if (!frame->keyframe)
            GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
        if (gst_app_src_push_buffer(GST_APP_SRC(p.input), buffer) != GST_FLOW_OK)
            return;
        std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample{
            gst_app_sink_try_pull_sample(GST_APP_SINK(p.output), 0), gst_sample_unref};
        if (!sample)
            continue;
        auto* decoded = gst_sample_get_buffer(sample.get());
        if (!decoded || !GST_BUFFER_PTS_IS_VALID(decoded))
            continue;
        const auto utc = static_cast<std::int64_t>(GST_BUFFER_PTS(decoded) / GST_USECOND);
        if (utc - last < 250000)
            continue;
        decltype(previous) image{};
        if (gst_buffer_extract(decoded, 0, image.data(), image.size()) != image.size())
            continue;
        if (!last || utc - last > 1500000) {
            previous = image;
            previous_cells.clear();
            last = utc;
            continue;
        }
        const std::string cells = archive::motion_sample(previous, image, 0).at("cells");
        previous = image;
        last = utc;
        v1::SubmitObservationRequest q;
        q.set_site_id(volume.owner().site);
        q.set_source_id(volume.owner().node);
        q.set_schema_version(1);
        q.set_type("motion");
        q.set_observation_id(archive::uuid());
        q.mutable_binding()->set_point_id(point);
        q.mutable_binding()->set_revision(binding);
        q.set_source_time_us(utc);
        if (frame->time.clock_epoch() && frame->time.uncertainty()) {
            q.mutable_clock()->set_epoch_id(frame->time.clock_epoch()->to_string());
            q.mutable_clock()->set_uncertainty_us(frame->time.uncertainty()->count());
        }
        q.set_attributes_json(
            archive::Json{{"cells", cells}, {"previous", previous_cells}, {"topic", "Local/Motion"}}.dump());
        previous_cells = cells;
        for (int attempt = 0; attempt < 2 && !stop.stop_requested(); ++attempt) {
            grpc::ClientContext ctx;
            ctx.set_deadline(std::chrono::system_clock::now() + 600ms);
            v1::RegistrationResult ack;
            if (catalog.ReportMotion(&ctx, q, &ack).error_code() != grpc::StatusCode::UNAVAILABLE)
                break;
        }
    }
}
} // namespace
void detect_motion(std::stop_token stop, Stream& stream, archive::Volume& volume, const std::string& point,
                   std::int64_t binding)
{
    auto catalog = v1::ArchiveCatalog::NewStub(grpc::CreateChannel(
        "unix:" + control::setting("BACURI_CATALOG_SOCKET"), grpc::experimental::LocalCredentials(UDS)));
    std::mutex mutex;
    std::unique_lock lock{mutex};
    std::condition_variable_any timer;
    while (!stop.stop_requested()) {
        try {
            if (volume.detector_enabled(point))
                decode(stop, stream, volume, point, binding, *catalog);
        } catch (const std::exception&) {
            std::fputs("Local detector stream unavailable\n", stderr);
        }
        timer.wait_for(lock, stop, 250ms, [] { return false; });
    }
}
} // namespace bacuri::vms::ingest
