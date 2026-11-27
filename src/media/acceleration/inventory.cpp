#include "media/acceleration/probe.hpp"
#include "media/acceleration/library.hpp"
#include "platform/scope_exit.hpp"
#include <gst/gst.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <cstdlib>
#include <array>
#include <fstream>
#include <map>
#include <sstream>
#include <set>

namespace bacuri::vms::acceleration
{
namespace fs = std::filesystem;
namespace
{
std::string line(const fs::path& path)
{
    std::ifstream input{path};
    std::string value;
    std::getline(input, value);
    return value;
}
std::string cpu_model()
{
    std::ifstream cpuinfo{"/proc/cpuinfo"};
    std::string field;
    while (std::getline(cpuinfo, field))
        if (field.starts_with("model name") && field.contains(": "))
            return field.substr(field.find(": ") + 2);
    return "CPU";
}
std::map<std::string, fs::path> render_nodes()
{
    std::map<std::string, fs::path> nodes;
    std::error_code ec;
    for (fs::directory_iterator it{"/dev/dri", ec}, end; !ec && it != end; it.increment(ec)) {
        const auto& entry = *it;
        const auto name = entry.path().filename().string();
        if (name.starts_with("renderD"))
            nodes.emplace(name, entry.path());
    }
    return nodes;
}
void va_driver_identity(std::ostream& out)
{
    std::set<fs::path> directories{"/usr/lib/dri", "/usr/lib64/dri", BACURI_VA_DRIVER_DIRECTORY};
    if (const auto* path = std::getenv("LIBVA_DRIVERS_PATH")) {
        std::istringstream paths{path};
        for (std::string entry; std::getline(paths, entry, ':');)
            if (!entry.empty())
                directories.emplace(std::move(entry));
    }
    std::map<fs::path, struct stat> drivers;
    for (const auto& directory : directories) {
        std::error_code ec;
        for (fs::directory_iterator it{directory, ec}, end; !ec && it != end; it.increment(ec)) {
            const auto& path = it->path();
            struct stat info{};
            if (path.filename().string().ends_with("_drv_video.so") && stat(path.c_str(), &info) == 0)
                drivers.emplace(path, info);
        }
    }
    for (const auto& [path, info] : drivers)
        out << '|' << path.string() << ':' << info.st_size << ':' << info.st_mtim.tv_sec << ':'
            << info.st_mtim.tv_nsec;
}
std::string kernel()
{
    utsname info{};
    return uname(&info) == 0 ? info.release : "unknown";
}
// Resolve the device-specific factory. Multi-GPU machines may assign a suffix to every additional device.
Json codecs(const std::string& backend, const std::string& device, unsigned ordinal)
{
    Json result = Json::object();
    auto* factories = gst_element_factory_list_get_elements(
        GST_ELEMENT_FACTORY_TYPE_ENCODER | GST_ELEMENT_FACTORY_TYPE_DECODER, GST_RANK_NONE);
    platform::ScopeExit release{[&] { gst_plugin_feature_list_free(factories); }};
    for (auto* item = factories; item; item = item->next) {
        auto* factory = GST_ELEMENT_FACTORY(item->data);
        const std::string name = GST_OBJECT_NAME(factory);
        if (!(backend == "nvidia" ? name.starts_with("nv") : name.starts_with("va")))
            continue;
        const auto codec = name.contains("h264") ? "h264" : name.contains("h265") ? "h265" : "";
        if (!*codec || (!name.ends_with("dec") && !name.ends_with("enc")))
            continue;
        if (name.starts_with("nvauto") || name.starts_with("v4l") || name.starts_with("vaapi"))
            continue;
        auto* element = gst_element_factory_create(factory, nullptr);
        if (!element)
            continue;
        platform::ScopeExit free_element{[&] { gst_object_unref(element); }};
        bool matches = false;
        if (backend == "nvidia" && g_object_class_find_property(G_OBJECT_GET_CLASS(element), "cuda-device-id")) {
            guint id{};
            g_object_get(element, "cuda-device-id", &id, nullptr);
            matches = id == ordinal;
        } else if (backend == "vaapi" &&
                   g_object_class_find_property(G_OBJECT_GET_CLASS(element), "device-path")) {
            gchar* path{};
            g_object_get(element, "device-path", &path, nullptr);
            matches = path && device == path;
            g_free(path);
        }
        if (matches) {
            auto key = std::string{codec} + (name.ends_with("dec") ? "_dec" : "_enc");
            if (!result.contains(key) || name.size() < result[key].get_ref<const std::string&>().size())
                result[key] = name;
        }
    }
    return result;
}
} // namespace
std::string hardware_identity()
{
    std::ostringstream identity;
    std::error_code ec;
    identity << cpu_model() << '|' << line("/sys/devices/system/cpu/online") << '|';
    for (const auto* name : {"CUDA_VISIBLE_DEVICES", "NVIDIA_VISIBLE_DEVICES", "LIBVA_DRIVER_NAME"})
        if (const auto* value = std::getenv(name))
            identity << name << '=' << value << '|';
    identity << kernel() << '|' << line("/proc/driver/nvidia/version") << '|'
             << (access("/dev/nvidiactl", R_OK | W_OK) == 0);
    std::map<std::string, std::string> cards;
    for (fs::directory_iterator it{"/proc/driver/nvidia/gpus", ec}, end; !ec && it != end; it.increment(ec)) {
        const auto& gpu = *it;
        std::ifstream file{gpu.path() / "information"};
        std::string field, value;
        while (std::getline(file, field)) {
            if (field.starts_with("GPU UUID:") || field.starts_with("Model:"))
                value += field;
            if (field.starts_with("Device Minor:")) {
                std::istringstream number{field.substr(field.find(':') + 1)};
                unsigned minor{};
                if (number >> minor)
                    value += field + (access(("/dev/nvidia" + std::to_string(minor)).c_str(), R_OK | W_OK) == 0
                                          ? " available"
                                          : " unavailable");
            }
        }
        cards[gpu.path().filename().string()] = value;
    }
    for (const auto& [id, info] : cards)
        identity << '|' << id << info;
    auto nodes = render_nodes();
    if (!nodes.empty())
        va_driver_identity(identity);
    for (const auto& [name, path] : nodes) {
        const auto sys = fs::path{"/sys/class/drm"} / name / "device";
        identity << '|' << name << fs::read_symlink(sys, ec).string() << line(sys / "device")
                 << line(sys / "vendor") << fs::read_symlink(sys / "driver", ec).string()
                 << line(sys / "driver/module/version") << (access(path.c_str(), R_OK | W_OK) == 0);
    }
    const auto text = identity.str();
    auto* digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, text.c_str(), -1);
    std::string result{digest};
    g_free(digest);
    return result;
}

Json inventory()
{
    gst_init(nullptr, nullptr);
    Json devices = Json::array({{{"id", "cpu"},
                                 {"backend", "cpu"},
                                 {"model", cpu_model()},
                                 {"driver", kernel()},
                                 {"memory_bytes", nullptr},
                                 {"codecs",
                                  {{"h264_dec", "avdec_h264"},
                                   {"h265_dec", "avdec_h265"},
                                   {"h264_enc", "x264enc"},
                                   {"h265_enc", "x265enc"}}}}});
    Library cuda{"libcuda.so.1"};
    if (cuda && cuda.symbol<int(unsigned)>("cuInit")(0) == 0) {
        int count{}, driver{};
        cuda.symbol<int(int*)>("cuDeviceGetCount")(&count);
        cuda.symbol<int(int*)>("cuDriverGetVersion")(&driver);
        for (int i = 0; i < count; ++i) {
            int dev{};
            std::array<char, 256> name{}, pci{};
            std::size_t bytes{};
            if (cuda.symbol<int(int*, int)>("cuDeviceGet")(&dev, i) ||
                cuda.symbol<int(char*, int, int)>("cuDeviceGetName")(name.data(), 256, dev) ||
                cuda.symbol<int(char*, int, int)>("cuDeviceGetPCIBusId")(pci.data(), 256, dev))
                continue;
            const bool known = cuda.symbol<int(std::size_t*, int)>("cuDeviceTotalMem_v2")(&bytes, dev) == 0;
            auto version = line("/sys/module/nvidia/version");
            devices.push_back({{"id", std::string{"nvidia:"} + pci.data()},
                               {"backend", "nvidia"},
                               {"ordinal", i},
                               {"model", name.data()},
                               {"driver", version.empty() ? std::to_string(driver) : version},
                               {"memory_bytes", known ? Json(bytes) : Json(nullptr)},
                               {"codecs", codecs("nvidia", "", static_cast<unsigned>(i))}});
        }
    }
    Library va{"libva.so.2"}, drm{"libva-drm.so.2"};
    for (const auto& [name, path] : render_nodes()) {
        if (!va || !drm)
            break;
        const int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;
        platform::ScopeExit close_fd{[fd] { close(fd); }};
        auto* display = drm.symbol<void*(int)>("vaGetDisplayDRM")(fd);
        int major{}, minor{};
        if (!display || va.symbol<int(void*, int*, int*)>("vaInitialize")(display, &major, &minor))
            continue;
        platform::ScopeExit end{[&] { va.symbol<int(void*)>("vaTerminate")(display); }};
        const char* vendor = va.symbol<const char*(void*)>("vaQueryVendorString")(display);
        const auto sys = fs::path{"/sys/class/drm"} / name / "device";
        Json memory = nullptr;
        const auto value = line(sys / "mem_info_vram_total");
        if (!value.empty() && value != "0")
            memory = std::stoull(value);
        devices.push_back({{"id", "vaapi:" + name},
                           {"backend", "vaapi"},
                           {"model", line(sys / "vendor") + ":" + line(sys / "device")},
                           {"driver", vendor ? vendor : "unknown"},
                           {"memory_bytes", memory},
                           {"codecs", codecs("vaapi", path.string(), 0)}});
    }
    return {{"state", "measured"}, {"devices", devices}};
}
} // namespace bacuri::vms::acceleration
