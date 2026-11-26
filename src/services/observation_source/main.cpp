#include "control.grpc.pb.h"
#include "platform/identifiers.hpp"
#include "platform/settings.hpp"
#include <glib.h>
#include <google/protobuf/util/json_util.h>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace
{
using namespace bacuri::vms;
void sample(const std::string& point, std::string_view version)
{
    std::int64_t revision{};
    auto [end, error] = std::from_chars(version.data(), version.data() + version.size(), revision);
    if (!platform::canonical_id(point) || error != std::errc{} || end != version.data() + version.size() ||
        revision < 1) {
        throw std::invalid_argument("Expected a point UUID and positive binding revision");
    }
    std::string epoch;
    std::ifstream boot{"/proc/sys/kernel/random/boot_id"};
    if (!(boot >> epoch) || !platform::canonical_id(epoch)) {
        throw std::runtime_error("Cannot read local clock epoch");
    }
    std::unique_ptr<gchar, decltype(&g_free)> id{g_uuid_string_random(), g_free};
    v1::SubmitObservationRequest q;
    q.set_observation_id(id.get());
    q.set_type("motion");
    q.set_schema_version(1);
    q.set_attributes_json("{}");
    q.mutable_binding()->set_point_id(point);
    q.mutable_binding()->set_revision(revision);
    q.set_source_time_us(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    q.mutable_clock()->set_epoch_id(epoch);
    q.mutable_clock()->set_uncertainty_us(1);
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(q, &json).ok()) {
        throw std::runtime_error("Cannot encode observation");
    }
    std::cout << json << '\n';
}
} // namespace
int main(int argc, char** argv)
try {
    using namespace bacuri::vms;
    if (argc == 4 && std::string_view{argv[1]} == "sample") {
        sample(argv[2], argv[3]);
        return 0;
    }
    if (argc != 1) {
        throw std::invalid_argument("Usage: bacuri-vms-observation-source [sample POINT REVISION]");
    }
    v1::SubmitObservationRequest q;
    std::array<char, 32769> buffer{};
    std::cin.read(buffer.data(), buffer.size());
    const auto size = static_cast<std::size_t>(std::cin.gcount());
    if (std::cin.bad() || size == buffer.size() ||
        !google::protobuf::util::JsonStringToMessage(std::string{buffer.data(), size}, &q).ok()) {
        throw std::runtime_error("Invalid observation JSON");
    }
    if (q.site_id().empty()) {
        q.set_site_id(platform::setting("BACURI_SITE_ID"));
    }
    if (q.source_id().empty()) {
        q.set_source_id(platform::setting("BACURI_OBSERVATION_SOURCE"));
    }
    auto stub = v1::ObservationIngest::NewStub(grpc::CreateChannel(
        "unix:" + platform::setting("BACURI_OBSERVATION_SOCKET"), grpc::experimental::LocalCredentials(UDS)));
    grpc::ClientContext rpc;
    rpc.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{5});
    v1::RegistrationResult result;
    auto status = stub->SubmitObservation(&rpc, q, &result);
    if (!status.ok()) {
        std::cerr << status.error_code() << " " << status.error_message() << '\n';
        return 1;
    }
    std::cout << v1::RegistrationState_Name(result.state()) << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
