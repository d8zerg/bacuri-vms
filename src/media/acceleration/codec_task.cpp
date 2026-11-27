#include "media/acceleration/codec_task.hpp"
#include "media/lease.hpp"
#include "media/acceleration/library.hpp"
#include <filesystem>
#include <array>
#include "platform/settings.hpp"
#include "platform/scope_exit.hpp"
#include "media/acceleration/encoder_sessions.hpp"
#include <glib.h>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace bacuri::vms::acceleration
{
namespace
{
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using Report = v1::TaskUpdate;
bool encoders_occupied(const CodecPath& path)
{
    if (path.backend != "nvidia" || !path.device.starts_with("nvidia:") || !path.encoder_sessions)
        return false;
    try {
        Library nvml{"libnvidia-ml.so.1"};
        if (!nvml)
            return false;
        const auto shutdown = nvml.symbol<int()>("nvmlShutdown");
        if (nvml.symbol<int()>("nvmlInit_v2")() != 0)
            return false;
        platform::ScopeExit close{[&] { shutdown(); }};
        void* handle{};
        if (nvml.symbol<int(const char*, void**)>("nvmlDeviceGetHandleByPciBusId_v2")(path.device.c_str() + 7,
                                                                                      &handle))
            return false;
        return encoder_sessions(nvml, handle).value_or(0) >= path.encoder_sessions;
    } catch (const std::runtime_error&) {
        return false;
    }
}
auto verified_factory(const std::string& name, const CodecPath& path, bool encoder = false)
{
    auto element = std::unique_ptr<GstElement, decltype(&gst_object_unref)>{
        gst_element_factory_make(name.c_str(), nullptr), gst_object_unref};
    // The nvcodec plugin can omit the encoder factory when its registration-time session open fails.
    if (!element && encoder && encoders_occupied(path))
        throw CodecFailure{"Encoder session unavailable", CodecFault::sessions};
    if (element && path.backend == "nvidia" &&
        g_object_class_find_property(G_OBJECT_GET_CLASS(element.get()), "cuda-device-id")) {
        guint ordinal{};
        g_object_get(element.get(), "cuda-device-id", &ordinal, nullptr);
        Library driver{"libcuda.so.1"};
        int device{};
        std::array<char, 256> address{};
        if (driver && driver.symbol<int(unsigned)>("cuInit")(0) == 0 &&
            driver.symbol<int(int*, int)>("cuDeviceGet")(&device, static_cast<int>(ordinal)) == 0 &&
            driver.symbol<int(char*, int, int)>("cuDeviceGetPCIBusId")(address.data(), 256, device) == 0 &&
            path.device == "nvidia:" + std::string{address.data()})
            return element;
    } else if (element && path.backend == "vaapi" &&
               g_object_class_find_property(G_OBJECT_GET_CLASS(element.get()), "device-path")) {
        gchar* raw{};
        g_object_get(element.get(), "device-path", &raw, nullptr);
        std::unique_ptr<gchar, decltype(&g_free)> device{raw, g_free};
        if (device && path.device == "vaapi:" + std::filesystem::path{device.get()}.filename().string())
            return element;
    }
    throw CodecFailure{"Assigned codec device unavailable", CodecFault::lost};
}
} // namespace
void pipeline_error(GstMessage* message)
{
    GError* raw{};
    gchar* details{};
    gst_message_parse_error(message, &raw, &details);
    std::unique_ptr<GError, decltype(&g_error_free)> error{raw, g_error_free};
    std::unique_ptr<gchar, decltype(&g_free)> debug{details, g_free};
    const std::string diagnostic = std::string{raw ? raw->message : ""} + " " + (details ? details : "");
    if (diagnostic.contains("DEVICE_LOST") || diagnostic.contains("DEVICE_NOT_EXIST"))
        throw CodecFailure{"Codec device lost", CodecFault::lost};
    if (diagnostic.contains("DEVICE_RESET") || diagnostic.contains("CONTEXT_IS_DESTROYED"))
        throw CodecFailure{"Codec device reset", CodecFault::reset};
    if (diagnostic.contains("ENCODER_BUSY") || diagnostic.contains("TOO_MANY_SESSIONS") ||
        (diagnostic.contains("OpenEncodeSession") && diagnostic.contains("OUT_OF_MEMORY")))
        throw CodecFailure{"Encoder session unavailable", CodecFault::sessions};
    if (raw && raw->domain == GST_RESOURCE_ERROR && GST_IS_ELEMENT(GST_MESSAGE_SRC(message))) {
        auto* factory = gst_element_get_factory(GST_ELEMENT(GST_MESSAGE_SRC(message)));
        const std::string_view source = factory ? GST_OBJECT_NAME(factory) : "";
        if (source == "filesrc" || source == "filesink")
            throw std::runtime_error{"Media file unavailable"};
    }
    throw CodecFailure{"Video pipeline failed"};
}
std::string CodecPath::decoder(const std::string& codec) const
{
    if (codec != "h264" && codec != "h265")
        throw std::invalid_argument("Unsupported input codec");
    if (grant.empty())
        return "avdec_" + codec + " max-threads=2";
    const auto factory = factories.find(codec + "_dec");
    if (factory == factories.end())
        throw CodecFailure("Assigned decoder unavailable");
    const auto element = verified_factory(factory->second, *this);
    if (backend == "nvidia" &&
        g_object_class_find_property(G_OBJECT_GET_CLASS(element.get()), "max-display-delay"))
        return factory->second + " max-display-delay=0";
    return factory->second;
}
std::string CodecPath::encoder(bool live) const
{
    if (grant.empty())
        return std::string{"x264enc threads=2 tune=zerolatency speed-preset=ultrafast "} +
               (live ? "bitrate=2000 key-int-max=25" : "pass=quant quantizer=16");
    const auto factory = factories.find("h264_enc");
    if (factory == factories.end())
        throw CodecFailure("Assigned encoder unavailable");
    verified_factory(factory->second, *this, true);
    if (backend == "nvidia")
        return factory->second + " zerolatency=true bframes=0 " +
               (live ? "bitrate=2000 gop-size=25" : "rc-mode=constqp qp-const-i=16 qp-const-p=16");
    return factory->second +
           (live ? " bitrate=2000 key-int-max=25" : " rate-control=cqp qpi=16 qpp=16 b-frames=0");
}
struct CodecTask::State {
    std::unique_ptr<v1::NodeAcceleration::Stub> stub;
    Report request;
    CodecPath choice;
    Clock::time_point next{};
    std::string held;
    void update(Report::Report report, const std::string& grant)
    {
        if (!stub)
            return;
        const auto start = Clock::now();
        next = start + 750ms;
        request.set_report(report);
        request.set_released_grant(grant);
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + 200ms);
        v1::TaskAssignment reply;
        choice = {};
        if (!stub->ScheduleTask(&context, request, &reply).ok())
            return;
        held = reply.grant();
        if (!held.empty() && (reply.backend() == "nvidia" || reply.backend() == "vaapi") &&
            Clock::now() < start + std::chrono::milliseconds{reply.valid_for_ms()}) {
            choice = {reply.device_id(), reply.backend(), held, {}, reply.verified_encoder_sessions()};
            for (const auto& [key, name] : reply.codecs())
                if (!name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == name.npos)
                    choice.factories.emplace(key, name);
            next = std::min(next, start + std::chrono::milliseconds{reply.valid_for_ms()} / 2);
        }
    }
};
CodecTask::CodecTask(const std::string& kind) : state_{std::make_unique<State>()}
{
    state_->request.set_task_kind(kind);
    const auto node = platform::optional_setting("BACURI_NODE_ID");
    if (node && platform::optional_setting("BACURI_MEDIA_SOCKET")) {
        state_->request.set_node_id(*node);
        std::unique_ptr<gchar, decltype(&g_free)> id{g_uuid_string_random(), g_free};
        state_->request.set_task_id(id.get());
        state_->stub = v1::NodeAcceleration::NewStub(media::local_channel("BACURI_MEDIA_SOCKET"));
    }
}
CodecTask::~CodecTask()
{
    if (state_->next != Clock::time_point{})
        state_->update(Report::FINISHED, state_->held);
}
const CodecPath& CodecTask::poll()
{
    if (state_->stub && Clock::now() >= state_->next)
        state_->update(Report::POLL, {});
    return state_->choice;
}
void CodecTask::check(const CodecPath& active)
{
    if (state_->stub && poll().grant != active.grant)
        throw Reassigned{};
}
void CodecTask::released(const CodecPath& active, CodecFault fault, bool stay_on_cpu)
{
    state_->request.clear_running_device_id();
    state_->request.clear_running_grant();
    state_->request.set_cpu_until_checkpoint(stay_on_cpu);
    constexpr std::array reports{Report::RELEASED, Report::ENCODER_ERROR, Report::SESSION_LIMIT,
                                 Report::DEVICE_LOST, Report::DRIVER_RESET};
    if (!active.grant.empty())
        state_->update(reports.at(static_cast<std::size_t>(fault)), active.grant);
    else if (stay_on_cpu)
        state_->update(Report::POLL, {});
    if (stay_on_cpu && !state_->held.empty())
        state_->update(Report::RELEASED, state_->held);
}
void CodecTask::next_checkpoint()
{
    if (state_->request.cpu_until_checkpoint()) {
        state_->request.set_cpu_until_checkpoint(false);
        state_->next = Clock::now();
    }
}
void CodecTask::used(const CodecPath& path)
{
    if (!state_->stub ||
        (state_->request.running_device_id() == path.device && state_->request.running_grant() == path.grant))
        return;
    state_->request.set_running_device_id(path.device);
    state_->request.set_running_grant(path.grant);
    state_->update(Report::POLL, {});
    const nlohmann::json row{{"task_id", state_->request.task_id()},
                             {"kind", state_->request.task_kind()},
                             {"running_device_id", path.device},
                             {"backend", path.backend}};
    std::fprintf(stderr, "%s\n", row.dump().c_str());
}
} // namespace bacuri::vms::acceleration
