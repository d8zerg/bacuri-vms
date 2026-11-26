#include "domain/archive.hpp"
#include "recorder/audio.hpp"
#include "recorder/rtsp_session.hpp"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/sdp/gstsdpmessage.h>
#include <algorithm>
#include <utility>

namespace bacuri::vms::ingest
{
namespace
{
using Pcm = domain::ArchiveSound;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
template <class T, auto Release> struct Deleter {
    void operator()(T* p) const
    {
        Release(p);
    }
};
template <class T, auto Release> using Ref = std::unique_ptr<T, Deleter<T, Release>>;
struct Pipeline {
    explicit Pipeline(const char* launch)
    {
        gst_init(nullptr, nullptr);
        GError* error{};
        element.reset(gst_parse_launch(launch, &error));
        Ref<GError, g_error_free> failure{error};
        if (!element || error)
            throw std::runtime_error("Audio pipeline unavailable");
    }
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    ~Pipeline()
    {
        gst_element_set_state(element.get(), GST_STATE_NULL);
    }
    void play()
    {
        if (gst_element_set_state(element.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            throw std::runtime_error("Audio pipeline failed");
    }
    Ref<GstElement, gst_object_unref> get(const char* name)
    {
        return Ref<GstElement, gst_object_unref>{gst_bin_get_by_name(GST_BIN(element.get()), name)};
    }
    void check()
    {
        Ref<GstBus, gst_object_unref> bus{gst_element_get_bus(element.get())};
        Ref<GstMessage, gst_message_unref> msg{
            // GStreamer represents message filter bitmasks with GstMessageType.
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            gst_bus_pop_filtered(bus.get(), static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))};
        if (msg)
            throw std::runtime_error("Audio stream ended");
    }
    Ref<GstElement, gst_object_unref> element;
};
std::int64_t now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
struct Backchannel {
    std::atomic<int> stream{-1}, payload{-1};
    std::atomic<bool> alaw{};
};
void describe(GstElement*, GstSDPMessage* sdp, gpointer data)
{
    auto& target = *static_cast<Backchannel*>(data);
    for (guint i = 0; i < gst_sdp_message_medias_len(sdp); ++i) {
        const auto* media = gst_sdp_message_get_media(sdp, i);
        if (g_strcmp0(gst_sdp_media_get_media(media), "audio") ||
            !gst_sdp_media_get_attribute_val(media, "sendonly"))
            continue;
        for (guint j = 0; j < gst_sdp_media_formats_len(media); ++j) {
            const int pt = std::atoi(gst_sdp_media_get_format(media, j));
            Ref<GstCaps, gst_caps_unref> caps{gst_sdp_media_get_caps_from_media(media, pt)};
            if (!caps || gst_caps_is_empty(caps.get()))
                continue;
            auto* fields = gst_caps_get_structure(caps.get(), 0);
            auto* name = gst_structure_get_string(fields, "encoding-name");
            int hz{};
            gst_structure_get_int(fields, "clock-rate", &hz);
            if (hz != 8000 || (g_strcmp0(name, "PCMA") && g_strcmp0(name, "PCMU")))
                continue;
            target.alaw = !g_strcmp0(name, "PCMA");
            target.payload = pt;
            target.stream = static_cast<int>(i);
            return;
        }
    }
}
} // namespace
CameraAudio::CameraAudio(Endpoint endpoint) : endpoint_(std::move(endpoint)) {}
CameraAudio::~CameraAudio()
{
    capture_.request_stop();
    transmit_.request_stop();
    ready_.notify_all();
    talk_ready_.notify_all();
}
Sound CameraAudio::listen(std::int64_t after)
{
    std::unique_lock lock(mutex_);
    listen_until_ = Clock::now() + 3s;
    if (!capture_.joinable())
        capture_ = std::jthread([this](const std::stop_token& stop) { capture(stop); });
    ready_.notify_all();
    const auto since = after > 0 ? std::max(after, now_us() - 1000000) : now_us() - 250000;
    ready_.wait_for(
        lock, 2s, [&] { return input_failed_ || (!samples_.empty() && samples_.back().sound.start_us >= since); });
    Sound result;
    for (const auto& sample : samples_) {
        if (sample.sound.start_us < since)
            continue;
        if (result.pcm.empty())
            result.start_us = sample.sound.start_us;
        if (result.pcm.size() + sample.sound.pcm.size() > 16000)
            break;
        result.pcm += sample.sound.pcm;
    }
    if (result.pcm.empty())
        throw std::runtime_error("Audio input unavailable");
    return result;
}
void CameraAudio::record(const std::string& owner, bool enable)
{
    std::unique_lock lock(mutex_);
    auto now = Clock::now();
    std::erase_if(record_leases_, [&](const auto& pair) { return pair.second <= now || pair.first == owner; });
    if (enable)
        record_leases_[owner] = now + 3s;
    record_until_ = {};
    for (auto& [id, until] : record_leases_)
        record_until_ = std::max(record_until_, until);
    if (!enable)
        return;

    if (!capture_.joinable())
        capture_ = std::jthread([this](const std::stop_token& stop) { capture(stop); });
    ready_.notify_all();
    if (!ready_.wait_for(lock, 2s,
                         [&] { return input_failed_ || (!samples_.empty() && samples_.back().record); }) ||
        input_failed_)
        throw std::runtime_error("Audio recording unavailable");
}
void CameraAudio::policy_record(bool enable)
{
    std::scoped_lock guard(mutex_);
    if (policy_recording_ == enable)
        return;
    policy_recording_ = enable;
    if (enable && !capture_.joinable())
        capture_ = std::jthread([this](const std::stop_token& stop) { capture(stop); });
    ready_.notify_all();
}
Sound CameraAudio::recorded(std::int64_t from, std::chrono::microseconds duration)
{
    const auto until = from + duration.count();
    std::scoped_lock lock(mutex_);
    Sound result;
    for (const auto& item : samples_) {
        if (!item.record)
            continue;
        const auto& s = item.sound;
        const auto samples = static_cast<std::int64_t>(s.pcm.size()) / Pcm::bytes_per_sample;
        auto cut = [&](auto t) {
            return std::clamp<std::int64_t>((t - s.start_us) / Pcm::sample_period_us, 0, samples);
        };
        auto begin = cut(from + Pcm::sample_period_us - 1), end = cut(until);
        if (end <= begin)
            continue;
        auto stamp = s.start_us + begin * Pcm::sample_period_us;
        const auto recorded = static_cast<std::int64_t>(result.pcm.size()) / Pcm::bytes_per_sample;
        if (result.pcm.empty())
            result.start_us = stamp;
        else if (std::abs(stamp - result.start_us - recorded * Pcm::sample_period_us) > 1000)
            break;
        result.pcm.append(s.pcm, static_cast<std::size_t>(begin * Pcm::bytes_per_sample),
                          static_cast<std::size_t>((end - begin) * Pcm::bytes_per_sample));
    }
    return result;
}
void CameraAudio::capture(const std::stop_token& stop)
{
    while (!stop.stop_requested()) {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, stop,
                    [&] { return policy_recording_ || std::max(record_until_, listen_until_) > Clock::now(); });
        if (stop.stop_requested())
            break;
        lock.unlock();
        try {
            Pipeline p{"rtspsrc name=camera protocols=tcp latency=100 tcp-timeout=2000000 "
                       "teardown-timeout=100000000 ! application/x-rtp,media=audio ! decodebin ! audioconvert ! "
                       "audioresample ! audio/x-raw,format=S16LE,rate=8000,channels=1,layout=interleaved ! "
                       "appsink name=output max-buffers=8 drop=true sync=false wait-on-eos=false"};
            auto src = p.get("camera"), out = p.get("output");
            if (!configure_rtsp_source(src.get(), endpoint_))
                throw std::runtime_error("Camera trust unavailable");
            g_signal_connect(src.get(), "select-stream",
                             G_CALLBACK(+[](GstElement*, guint, GstCaps* caps, gpointer) -> gboolean {
                                 return !g_strcmp0(
                                     gst_structure_get_string(gst_caps_get_structure(caps, 0), "media"), "audio");
                             }),
                             nullptr);
            p.play();
            std::int64_t next{};
            auto last = Clock::now();
            while (!stop.stop_requested()) {
                {
                    std::scoped_lock guard(mutex_);
                    if (!policy_recording_ && Clock::now() >= std::max(record_until_, listen_until_))
                        break;
                }
                Ref<GstSample, gst_sample_unref> sample{
                    gst_app_sink_try_pull_sample(GST_APP_SINK(out.get()), 100 * GST_MSECOND)};
                p.check();
                if (!sample) {
                    if (Clock::now() - last > 2s)
                        throw std::runtime_error("No audio samples");
                    continue;
                }
                last = Clock::now();
                auto* b = gst_sample_get_buffer(sample.get());
                auto size = gst_buffer_get_size(b);
                if (!size || size > static_cast<std::size_t>(Pcm::max_chunk_bytes) ||
                    size % static_cast<std::size_t>(Pcm::bytes_per_sample))
                    continue;
                auto length = static_cast<std::int64_t>(size) / Pcm::bytes_per_sample * Pcm::sample_period_us;
                auto received = now_us() - length;
                if (!next || std::abs(received - next) > 200000)
                    next = received;
                Sound sound{next, std::string(size, '\0')};
                gst_buffer_extract(b, 0, sound.pcm.data(), size);
                next += length;
                std::scoped_lock guard(mutex_);
                input_failed_ = false;
                samples_.push_back({std::move(sound), policy_recording_ || Clock::now() < record_until_});
                while (!samples_.empty() &&
                       (next - samples_.front().sound.start_us > 18000000 || samples_.size() > 2048))
                    samples_.pop_front();
                ready_.notify_all();
            }
        } catch (const std::exception&) {
            std::unique_lock guard(mutex_);
            input_failed_ = true;
            record_until_ = listen_until_ = Clock::time_point{};
            ready_.notify_all();
            ready_.wait_for(guard, stop, 250ms, [] { return false; });
            input_failed_ = false;
        }
    }
}
void CameraAudio::speak(const std::string& session, std::chrono::milliseconds ttl, const std::string& pcm)
{
    if (pcm.empty() || pcm.size() > 8000 || pcm.size() % 320)
        throw std::invalid_argument("Invalid speech packet");
    std::unique_lock lock(talk_mutex_);
    if (session.empty() || ttl <= 0ms || ttl > 3s)
        throw std::invalid_argument("Invalid speech lease");
    if (talk_session_ != session) {
        speech_.clear();
        talk_session_ = session;
    }
    talk_until_ = Clock::now() + std::min(ttl, 2000ms);
    if (!transmit_.joinable())
        transmit_ = std::jthread([this](const std::stop_token& stop) { transmit(stop); });
    talk_ready_.notify_all();
    if (!talk_ready_.wait_for(lock, 2s, [&] { return output_ready_ || output_failed_; }) || !output_ready_ ||
        speech_.size() >= 2)
        throw std::runtime_error("Camera backchannel unavailable");
    speech_.push_back(pcm);
    talk_ready_.notify_all();
}
void CameraAudio::mute(const std::string& session)
{
    std::scoped_lock lock(talk_mutex_);
    if (talk_session_ != session)
        return;
    talk_session_.clear();
    speech_.clear();
    talk_until_ = {};
    talk_ready_.notify_all();
}
void CameraAudio::transmit(const std::stop_token& stop)
{
    while (!stop.stop_requested()) {
        std::unique_lock lock(talk_mutex_);
        talk_ready_.wait(lock, stop, [&] { return talk_until_ > Clock::now(); });
        if (stop.stop_requested())
            break;
        lock.unlock();
        try {
            Backchannel back;
            Pipeline p{"rtspsrc name=camera protocols=tcp backchannel=onvif tcp-timeout=1500000 "
                       "teardown-timeout=100000000 ! fakesink async=false sync=false"};
            auto src = p.get("camera");
            if (!configure_rtsp_source(src.get(), endpoint_))
                throw std::runtime_error("Camera trust unavailable");
            g_signal_connect(src.get(), "on-sdp", G_CALLBACK(describe), &back);
            g_signal_connect(src.get(), "select-stream",
                             G_CALLBACK(+[](GstElement*, guint id, GstCaps*, gpointer state) -> gboolean {
                                 return static_cast<int>(id) == static_cast<Backchannel*>(state)->stream.load();
                             }),
                             &back);
            p.play();
            const auto deadline = Clock::now() + 1800ms;
            Ref<GstBus, gst_object_unref> bus{gst_element_get_bus(p.element.get())};
            bool playing = false;
            while (!playing && !stop.stop_requested() && Clock::now() < deadline) {
                Ref<GstMessage, gst_message_unref> message{gst_bus_timed_pop_filtered(
                    bus.get(), 40 * GST_MSECOND,
                    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
                    static_cast<GstMessageType>(GST_MESSAGE_PROGRESS | GST_MESSAGE_EOS | GST_MESSAGE_ERROR))};
                if (!message)
                    continue;
                if (GST_MESSAGE_TYPE(message.get()) != GST_MESSAGE_PROGRESS)
                    throw std::runtime_error("Backchannel setup failed");
                const auto* state = gst_message_get_structure(message.get());
                int type{};
                if (GST_MESSAGE_SRC(message.get()) == GST_OBJECT(src.get()) &&
                    gst_structure_get_enum(state, "type", GST_TYPE_PROGRESS_TYPE, &type) &&
                    type == GST_PROGRESS_TYPE_COMPLETE &&
                    !g_strcmp0(gst_structure_get_string(state, "code"), "request"))
                    playing = true;
            }
            if (!playing || back.stream < 0)
                throw std::runtime_error("Backchannel PLAY unavailable");
            const auto launch =
                std::string("appsrc name=input format=time "
                            "caps=audio/x-raw,format=S16LE,rate=8000,channels=1,layout=interleaved ! ") +
                (back.alaw ? "alawenc ! rtppcmapay" : "mulawenc ! rtppcmupay") +
                " pt=" + std::to_string(back.payload.load()) + " ! appsink name=rtp sync=false wait-on-eos=false";
            Pipeline encoder{launch.c_str()};
            auto input = encoder.get("input"), rtp = encoder.get("rtp");
            encoder.play();
            std::uint64_t pts{};
            auto due = Clock::now();
            for (;;) {
                p.check();
                std::unique_lock guard(talk_mutex_);
                output_ready_ = true;
                talk_ready_.notify_all();
                talk_ready_.wait_until(guard, stop, talk_until_,
                                       [&] { return !speech_.empty() || talk_until_ <= Clock::now(); });
                if (stop.stop_requested() || Clock::now() >= talk_until_)
                    break;
                if (speech_.empty())
                    continue;
                const auto session = talk_session_;
                auto chunk = std::move(speech_.front());
                speech_.pop_front();
                guard.unlock();
                due = std::max(due, Clock::now());
                for (size_t pos = 0; pos < chunk.size(); pos += 320) {
                    std::this_thread::sleep_until(due);
                    due += 20ms;
                    {
                        std::scoped_lock check(talk_mutex_);
                        if (stop.stop_requested() || session != talk_session_ || Clock::now() >= talk_until_)
                            break;
                    }
                    auto* buffer = gst_buffer_new_allocate(nullptr, 320, nullptr);
                    gst_buffer_fill(buffer, 0, chunk.data() + pos, 320);
                    GST_BUFFER_PTS(buffer) = pts;
                    GST_BUFFER_DURATION(buffer) = 20 * GST_MSECOND;
                    pts += 20 * GST_MSECOND;
                    if (gst_app_src_push_buffer(GST_APP_SRC(input.get()), buffer) != GST_FLOW_OK)
                        throw std::runtime_error("Speech encoder unavailable");
                    Ref<GstSample, gst_sample_unref> packet{
                        gst_app_sink_try_pull_sample(GST_APP_SINK(rtp.get()), 100 * GST_MSECOND)};
                    GstFlowReturn flow = GST_FLOW_ERROR;
                    if (packet)
                        g_signal_emit_by_name(src.get(), "push-backchannel-sample",
                                              static_cast<guint>(back.stream), packet.get(), &flow);
                    if (flow != GST_FLOW_OK)
                        throw std::runtime_error("Backchannel write failed");
                }
            }
        } catch (const std::exception&) {
            std::scoped_lock guard(talk_mutex_);
            output_failed_ = true;
            talk_ready_.notify_all();
        }
        std::unique_lock guard(talk_mutex_);
        output_ready_ = false;
        talk_until_ = {};
        speech_.clear();
        talk_ready_.wait_for(guard, stop, 100ms, [] { return false; });
        output_failed_ = false;
    }
}
} // namespace bacuri::vms::ingest
