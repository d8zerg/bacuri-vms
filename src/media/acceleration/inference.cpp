#include "media/acceleration/probe.hpp"
#include "media/acceleration/library.hpp"
#include "platform/scope_exit.hpp"
#include <array>
#include <algorithm>
#include <limits>
#include <cstdlib>

namespace bacuri::vms::acceleration
{
namespace
{
using Frame = std::array<float, 256>;
using Clock = std::chrono::steady_clock;
void check(int code)
{
    if (code != 0)
        throw std::runtime_error("Inference backend failed");
}
template <class Run> Json timed(const Frame& input, Run run)
{
    Frame output{};
    const auto begin = Clock::now();
    constexpr unsigned passes = 32;
    for (unsigned i = 0; i < passes; ++i) {
        output.fill(std::numeric_limits<float>::quiet_NaN());
        run(output);
        for (std::size_t j = 0; j < input.size(); ++j)
            if (output[j] != std::max(0.F, input[j]))
                throw std::runtime_error("Inference output mismatch");
    }
    auto us = std::max<std::int64_t>(
        1, std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count());
    return {{"state", "measured"}, {"frames", passes}, {"elapsed_us", us}};
}
Json cuda_infer(int device, const Frame& input)
{
    Library cuda{"libcuda.so.1"};
    if (!cuda)
        return {{"state", "unsupported"}};
    check(cuda.symbol<int(unsigned)>("cuInit")(0));
    void* context{};
    check(cuda.symbol<int(void**, unsigned, int)>("cuCtxCreate_v2")(&context, 0, device));
    platform::ScopeExit end{[&] { cuda.symbol<int(void*)>("cuCtxDestroy_v2")(context); }};
    static constexpr char ptx[] = R"(.version 6.0
.target sm_52
.address_size 64
.visible .entry relu(.param .u64 address) {
.reg .u64 base, offset;
.reg .u32 index;
.reg .f32 value;
ld.param.u64 base, [address];
mov.u32 index, %tid.x;
mul.wide.u32 offset, index, 4;
add.u64 base, base, offset;
ld.global.f32 value, [base];
max.f32 value, value, 0f00000000;
st.global.f32 [base], value;
ret;
})";
    void *module{}, *function{};
    unsigned long long data{};
    check(cuda.symbol<int(void**, const void*)>("cuModuleLoadData")(&module, ptx));
    check(cuda.symbol<int(void**, void*, const char*)>("cuModuleGetFunction")(&function, module, "relu"));
    check(cuda.symbol<int(unsigned long long*, std::size_t)>("cuMemAlloc_v2")(&data, sizeof(input)));
    auto upload = cuda.symbol<int(unsigned long long, const void*, std::size_t)>("cuMemcpyHtoD_v2");
    auto download = cuda.symbol<int(void*, unsigned long long, std::size_t)>("cuMemcpyDtoH_v2");
    auto launch = cuda.symbol<int(void*, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                                  void*, void**, void**)>("cuLaunchKernel");
    return timed(input, [&](Frame& output) {
        check(upload(data, input.data(), sizeof(input)));
        void* arg = &data;
        check(launch(function, 1, 1, 1, 256, 1, 1, 0, nullptr, &arg, nullptr));
        check(download(output.data(), data, sizeof(output)));
    });
}
Json openvino(const Frame& input)
{
    const auto* path = std::getenv("BACURI_OPENVINO_LIBRARY");
    Library ov{path && *path ? path : "libopenvino_c.so"};
    if (!ov)
        return {{"state", "unsupported"}};
    static constexpr char model[] = R"(<net name="probe" version="11"><layers>
<layer id="0" name="frame" type="Parameter" version="opset1"><data shape="1,256" element_type="f32"/><output><port id="0" precision="FP32"><dim>1</dim><dim>256</dim></port></output></layer>
<layer id="1" name="relu" type="ReLU" version="opset1"><input><port id="0"><dim>1</dim><dim>256</dim></port></input><output><port id="1" precision="FP32"><dim>1</dim><dim>256</dim></port></output></layer>
<layer id="2" name="out" type="Result" version="opset1"><input><port id="0"><dim>1</dim><dim>256</dim></port></input></layer></layers>
<edges><edge from-layer="0" from-port="0" to-layer="1" to-port="0"/><edge from-layer="1" from-port="1" to-layer="2" to-port="0"/></edges></net>)";
    void *core{}, *network{};
    check(ov.symbol<int(void**)>("ov_core_create")(&core));
    platform::ScopeExit free_core{[&] { ov.symbol<void(void*)>("ov_core_free")(core); }};
    check(ov.symbol<int(void*, const char*, std::size_t, void*, void**)>("ov_core_read_model_from_memory_buffer")(
        core, model, sizeof(model) - 1, nullptr, &network));
    platform::ScopeExit free_model{[&] { ov.symbol<void(void*)>("ov_model_free")(network); }};
    struct Devices {
        char** names;
        std::size_t size;
    } devices{};
    check(ov.symbol<int(void*, Devices*)>("ov_core_get_available_devices")(core, &devices));
    platform::ScopeExit free_devices{[&] { ov.symbol<void(Devices*)>("ov_available_devices_free")(&devices); }};
    Json targets = Json::array();
    bool measured = false;
    for (std::size_t i = 0; i < devices.size; ++i) {
        Json result;
        try {
            void *compiled{}, *request{}, *in{}, *out{};
            check(ov.symbol<int(void*, void*, const char*, std::size_t, void**, ...)>("ov_core_compile_model")(
                core, network, devices.names[i], 0, &compiled));
            platform::ScopeExit free_compiled{[&] { ov.symbol<void(void*)>("ov_compiled_model_free")(compiled); }};
            check(ov.symbol<int(void*, void**)>("ov_compiled_model_create_infer_request")(compiled, &request));
            platform::ScopeExit free_request{[&] { ov.symbol<void(void*)>("ov_infer_request_free")(request); }};
            check(ov.symbol<int(void*, void**)>("ov_infer_request_get_input_tensor")(request, &in));
            platform::ScopeExit free_in{[&] { ov.symbol<void(void*)>("ov_tensor_free")(in); }};
            check(ov.symbol<int(void*, void**)>("ov_infer_request_get_output_tensor")(request, &out));
            platform::ScopeExit free_out{[&] { ov.symbol<void(void*)>("ov_tensor_free")(out); }};
            void *src{}, *dst{};
            auto ptr = ov.symbol<int(void*, void**)>("ov_tensor_data");
            check(ptr(in, &src));
            check(ptr(out, &dst));
            auto infer = ov.symbol<int(void*)>("ov_infer_request_infer");
            result = timed(input, [&](Frame& output) {
                std::ranges::copy(input, static_cast<float*>(src));
                check(infer(request));
                std::copy_n(static_cast<float*>(dst), output.size(), output.begin());
            });
            measured = true;
        } catch (const std::exception&) {
            result = {{"state", "failed"}};
        }
        result["device"] = devices.names[i];
        targets.push_back(std::move(result));
    }
    return {{"state", measured ? "measured" : (targets.empty() ? "unsupported" : "failed")}, {"targets", targets}};
}
} // namespace
Json measure_inference(const Json& request)
{
    Frame input{};
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(i) - 128.F;
    auto backend = request.at("operation").get<std::string>();
    if (backend == "openvino")
        return openvino(input);
    if (backend == "cuda")
        return cuda_infer(request.at("device").at("ordinal").get<int>(), input);
    return timed(input, [&](Frame& result) {
        std::ranges::transform(input, result.begin(), [](float value) { return std::max(0.F, value); });
    });
}
} // namespace bacuri::vms::acceleration
