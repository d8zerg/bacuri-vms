#pragma once
#include <functional>
#include <optional>
#include <map>
#include "domain/image_region.hpp"
#include "archive/manifest.hpp"
#include "application/export/export.hpp"
#include <fstream>

namespace bacuri::vms::control
{
namespace fs = std::filesystem;
struct ExportSlot {
    ExportSlot();
    ExportSlot(const ExportSlot&) = delete;
    ExportSlot& operator=(const ExportSlot&) = delete;
    ~ExportSlot();
};
class ExportBody final : public MediaBody
{
  public:
    explicit ExportBody(std::shared_ptr<ExportSlot> capacity);
    ExportBody(const ExportBody&) = delete;
    ExportBody& operator=(const ExportBody&) = delete;
    ~ExportBody() override;
    std::chrono::steady_clock::time_point authorize();
    MediaChunk next() override;
    std::shared_ptr<ExportSlot> slot;
    std::function<std::chrono::steady_clock::time_point()> group_lease;
    postgresql::Database db;
    fs::path root;
    std::string browser, grant, point, begin, end;
    std::map<std::string, std::string> hashes;
    bool derivative{}, sound{};
    domain::ImageRegions regions;
    std::ifstream input;
    std::chrono::steady_clock::time_point deadline{};
};
void write_text(const fs::path& path, const std::string& value);
void pack(const fs::path& root, const std::vector<std::string>& clips);
struct ExportedAudio {
    std::string file;
    archive::MediaDigest digest;
    std::int64_t begin{}, end{};
};
std::optional<ExportedAudio> export_audio(const archive::BlockManifest& source, std::int64_t begin,
                                          std::int64_t end, const fs::path& dir, const std::string& file);
std::shared_ptr<ExportBody> prepare_fragment(const Request& req, Response& res, const Sessions& sessions,
                                             const Key& key, const std::string& site, const std::string& point,
                                             const std::shared_ptr<ExportSlot>& capacity,
                                             const fs::path& material = {});
} // namespace bacuri::vms::control
