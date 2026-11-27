#include "media/live_codec.hpp"
#include "platform/scope_exit.hpp"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <mutex>

namespace bacuri::vms::media
{
namespace
{
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
template <class T> auto own(T* value)
{
    return std::unique_ptr<T, decltype(&gst_object_unref)>{value, gst_object_unref};
}
using acceleration::CodecFailure;
} // namespace
struct LiveCodec::Pipeline {
    decltype(own(static_cast<GstElement*>(nullptr))) pipe{nullptr, gst_object_unref},
        input{nullptr, gst_object_unref}, output{nullptr, gst_object_unref};
    std::size_t skip;
    std::mutex clock_mutex;
    std::deque<GstClockTime> timestamps;
    Pipeline(const std::string& codec, const acceleration::CodecPath& path, PrivacyMask& mask, bool analysis,
             std::size_t already_sent)
        : skip{already_sent}
    {
        gst_init(nullptr, nullptr);
        auto spec = "appsrc name=input format=time caps=video/x-" + codec +
                    ",alignment=au,stream-format=byte-stream ! " + codec + "parse ! " + path.decoder(codec) +
                    " ! videoconvert ! ";
        if (analysis)
            spec += "videoscale ! video/x-raw,format=GRAY8,width=64,height=48,pixel-aspect-ratio=1/1 ! identity "
                    "name=checkpoint";
        else
            spec += "video/x-raw,format=I420 ! identity name=checkpoint ! videoconvert ! " + path.encoder(true) +
                    " ! video/x-h264,profile=constrained-baseline ! h264parse config-interval=-1 ! "
                    "video/x-h264,stream-format=byte-stream,alignment=au";
        spec += " ! appsink name=output max-buffers=4 sync=false async=false";
        GError* err{};
        pipe.reset(gst_parse_launch(spec.c_str(), &err));
        std::unique_ptr<GError, decltype(&g_error_free)> error{err, g_error_free};
        if (!pipe || error)
            throw CodecFailure{"Live codec unavailable"};
        input.reset(gst_bin_get_by_name(GST_BIN(pipe.get()), "input"));
        output.reset(gst_bin_get_by_name(GST_BIN(pipe.get()), "output"));
        auto checkpoint = own(gst_bin_get_by_name(GST_BIN(pipe.get()), "checkpoint"));
        auto pad = own(gst_element_get_static_pad(checkpoint.get(), "sink"));
        // Save the decoder's presentation clock before the encoder can apply its own timestamp offset.
        // The new encoder sees only unpublished pictures, so the receiver can decode its first IDR.
        gst_pad_add_probe(
            pad.get(), GST_PAD_PROBE_TYPE_BUFFER,
            [](GstPad*, GstPadProbeInfo* info, gpointer data) {
                auto& pipeline = *static_cast<Pipeline*>(data);
                if (pipeline.skip) {
                    --pipeline.skip;
                    return GST_PAD_PROBE_DROP;
                }
                std::scoped_lock guard{pipeline.clock_mutex};
                pipeline.timestamps.push_back(GST_BUFFER_PTS(GST_PAD_PROBE_INFO_BUFFER(info)));
                return GST_PAD_PROBE_OK;
            },
            this, nullptr);
        mask.attach(checkpoint.get());
    }
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    ~Pipeline()
    {
        gst_element_set_state(pipe.get(), GST_STATE_NULL);
    }
    void check() const
    {
        auto bus = own(gst_element_get_bus(pipe.get()));
        if (auto* message = gst_bus_pop_filtered(bus.get(), GST_MESSAGE_ERROR)) {
            platform::ScopeExit release{[&] { gst_message_unref(message); }};
            acceleration::pipeline_error(message);
        }
    }
};
LiveCodec::LiveCodec(std::string codec, PrivacyMask mask, Format format)
    : codec_{std::move(codec)}, analysis_{format == Format::gray}, mask_{std::move(mask)},
      task_{analysis_       ? "live_analysis"
            : mask_.empty() ? "live_profile"
                            : "live_mask"}
{
}
LiveCodec::~LiveCodec() = default;
void LiveCodec::trim()
{
    // A keyframe can replace old decode context only after every earlier picture was emitted.
    const auto consumed = published_ - first_;
    std::size_t discard = cpu_from_ ? std::min(consumed, history_.size()) : 0;
    for (std::size_t i = std::min(consumed + 1, history_.size()); i > 0; --i)
        if (history_[i - 1].keyframe && (!cpu_from_ || first_ + i - 1 >= *cpu_from_)) {
            if (cpu_from_) {
                cpu_from_.reset();
                task_.next_checkpoint();
            }
            discard = i - 1;
            break;
        }
    first_ += discard;
    while (discard--) {
        bytes_ -= history_.front().bytes.size();
        history_.pop_front();
    }
}
void LiveCodec::push(std::string_view bytes, bool keyframe, GstClockTime pts, const Output& output)
{
    if (keyframe && !cpu_from_)
        task_.next_checkpoint();
    if (!pipe_ && history_.empty() && !keyframe)
        return;
    constexpr auto max_bytes = 32uz * 1024 * 1024;
    constexpr auto max_frames = 900uz;
    if (bytes.size() > max_bytes)
        throw std::runtime_error{"Oversize access unit"};
    if (history_.size() >= max_frames || bytes_ > max_bytes - bytes.size()) {
        // A long GOP can outgrow replay storage. Reconstruct it on CPU while the context still fits,
        // then keep that decoder until a new published keyframe makes replay possible again.
        if (!cpu_from_) {
            if (!path_.grant.empty())
                pipe_.reset();
            task_.released(path_, acceleration::CodecFault::none, true);
            cpu_from_ = first_ + history_.size();
            pump(output, false);
            trim();
        }
        if (history_.size() >= max_frames || bytes_ > max_bytes - bytes.size())
            throw std::runtime_error{"Live codec backlog exceeded"};
    }
    if (first_ + history_.size() == published_)
        progress_ = Clock::now();
    history_.push_back({std::string{bytes}, keyframe, pts});
    bytes_ += bytes.size();
    pump(output, false);
}
void LiveCodec::poll(const Output& output)
{
    if (!history_.empty())
        pump(output, false);
}
void LiveCodec::finish(const Output& output)
{
    if (!history_.empty())
        pump(output, true);
}
void LiveCodec::drain(const Output& output, GstClockTime wait)
{
    while (auto* raw = gst_app_sink_try_pull_sample(GST_APP_SINK(pipe_->output.get()), wait)) {
        std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample{raw, gst_sample_unref};
        auto* buf = gst_sample_get_buffer(sample.get());
        if (!buf || published_ >= fed_)
            throw CodecFailure{"Unexpected live frame"};
        GstMapInfo map{};
        if (!gst_buffer_map(buf, &map, GST_MAP_READ))
            throw CodecFailure{"Unreadable live frame"};
        platform::ScopeExit unmap{[&] { gst_buffer_unmap(buf, &map); }};
        GstClockTime pts{};
        {
            std::scoped_lock guard{pipe_->clock_mutex};
            if (pipe_->timestamps.empty())
                throw CodecFailure{"Missing live frame timestamp"};
            pts = pipe_->timestamps.front();
            pipe_->timestamps.pop_front();
        }
        output({reinterpret_cast<const char*>(map.data), map.size},
               !GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT), pts, path_.backend);
        ++published_;
        progress_ = Clock::now();
        task_.used(path_);
        task_.check(path_);
        wait = 0;
    }
    pipe_->check();
    trim();
}
void LiveCodec::pump(const Output& output, bool finish)
{
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        const auto& assigned = task_.poll();
        if (pipe_ && assigned.grant != path_.grant) {
            pipe_.reset();
            task_.released(path_, acceleration::CodecFault::none);
        }
        try {
            if (!pipe_) {
                path_ = task_.poll();
                pipe_ = std::make_unique<Pipeline>(codec_, path_, mask_, analysis_, published_ - first_);
                if (gst_element_set_state(pipe_->pipe.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
                    throw CodecFailure{"Cannot start live codecs"};
                fed_ = first_;
                progress_ = Clock::now();
            }
            while (fed_ < first_ + history_.size()) {
                task_.check(path_);
                const auto& frame = history_[fed_ - first_];
                auto* buf = gst_buffer_new_allocate(nullptr, frame.bytes.size(), nullptr);
                if (!buf)
                    throw std::bad_alloc{};
                gst_buffer_fill(buf, 0, frame.bytes.data(), frame.bytes.size());
                GST_BUFFER_PTS(buf) = frame.pts;
                if (!frame.keyframe)
                    GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
                if (gst_app_src_push_buffer(GST_APP_SRC(pipe_->input.get()), buf) != GST_FLOW_OK)
                    throw CodecFailure{"Live codec input closed"};
                ++fed_;
            }
            if (finish && gst_app_src_end_of_stream(GST_APP_SRC(pipe_->input.get())) != GST_FLOW_OK)
                throw CodecFailure{"Cannot flush live codecs"};
            do {
                drain(output, finish ? GST_SECOND / 20 : 0);
                // A slow camera can legitimately have a few pictures pending decoder reordering.
                if (fed_ > published_ && (finish || fed_ - published_ > 32) && Clock::now() - progress_ > 2s)
                    throw CodecFailure{"Live codec stalled"};
                if (finish)
                    task_.check(path_);
            } while (finish && fed_ != published_);
            return;
        } catch (const acceleration::Reassigned&) {
            pipe_.reset();
            task_.released(path_, acceleration::CodecFault::none);
        } catch (const CodecFailure& error) {
            pipe_.reset();
            if (path_.grant.empty() || attempt)
                throw;
            task_.released(path_, error.fault, true);
        }
    }
    throw std::runtime_error{"Live codec recovery failed"};
}
} // namespace bacuri::vms::media
