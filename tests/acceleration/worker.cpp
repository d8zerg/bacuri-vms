#include "media/lease.hpp"
#include "platform/settings.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <gst/gst.h>
#include "platform/scope_exit.hpp"

namespace
{
// Preroll real encoder sessions and hold them without encoding a continuous stream.
int hold(const std::string& factory, std::size_t count)
{
    if (count > 16)
        return 2;
    gst_init(nullptr, nullptr);
    std::vector<std::unique_ptr<GstElement, decltype(&gst_object_unref)>> pipelines;
    bacuri::vms::platform::ScopeExit close{[&] {
        for (auto& pipe : pipelines)
            gst_element_set_state(pipe.get(), GST_STATE_NULL);
    }};
    for (unsigned i = 0; i < count; ++i) {
        const auto line =
            "videotestsrc ! video/x-raw,width=320,height=180 ! videoconvert ! " + factory + " ! fakesink";
        pipelines.emplace_back(gst_parse_launch(line.c_str(), nullptr), gst_object_unref);
        auto* pipe = pipelines.back().get();
        if (!pipe || gst_element_set_state(pipe, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE ||
            gst_element_get_state(pipe, nullptr, nullptr, 2 * GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
            std::cout << "unavailable\n" << std::flush;
            return 3;
        }
    }
    std::cout << "ready\n" << std::flush;
    std::string done;
    std::getline(std::cin, done);
    return 0;
}
} // namespace

int main(int argc, char** argv)
try {
    using namespace bacuri::vms;
    if (argc == 4 && std::string_view{argv[1]} == "--hold-encoders")
        return hold(argv[2], std::stoul(argv[3]));
    if (argc != 5)
        return 2;
    v1::TaskUpdate update;
    update.set_node_id(platform::setting("BACURI_NODE_ID"));
    update.set_task_id(argv[1]);
    update.set_task_kind(argv[2]);
    v1::TaskUpdate_Report report{};
    if (!v1::TaskUpdate_Report_Parse(argv[3], &report))
        return 2;
    update.set_report(report);
    update.set_released_grant(argv[4]);
    auto stub = v1::NodeAcceleration::NewStub(media::local_channel("BACURI_MEDIA_SOCKET"));
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{2});
    v1::TaskAssignment reply;
    const auto result = stub->ScheduleTask(&context, update, &reply);
    if (!result.ok())
        throw std::runtime_error(result.error_message());
    std::cout << nlohmann::json{{"device", reply.device_id()},
                                {"grant", reply.grant()},
                                {"reason", reply.reason()}}
              << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
