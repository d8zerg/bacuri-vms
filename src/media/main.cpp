#include "archive/replica.hpp"
#include "platform/rpc_status.hpp"
#include "archive/mp4.hpp"
#include "archive/clip.hpp"
#include <sys/mman.h>
#include <deque>
#include <mutex>
#include "platform/identifiers.hpp"
#include "platform/settings.hpp"
#include "media/lease.hpp"
#include "media/live.hpp"
#include "observability/metrics.hpp"
#include <gst/gst.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <fcntl.h>
#include <google/protobuf/util/time_util.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <array>
#include <cstdio>
#include <csignal>

namespace bacuri::vms::media
{
namespace
{
class Descriptor
{
  public:
    explicit Descriptor(int descriptor) : fd{descriptor}
    {
        if (fd < 0) {
            throw std::runtime_error("Archive file unavailable");
        }
    }
    ~Descriptor()
    {
        close(fd);
    }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    int fd;
};

class Delivery final : public v1::ArchiveDelivery::Service
{
  public:
    grpc::Status ReadPart(grpc::ServerContext* context, const v1::ReadPartRequest* request,
                          grpc::ServerWriter<v1::PartChunk>* stream) override
    {
        if (!request->scope().has_archive() || !platform::canonical_id(request->block_id())) {
            return platform::rpc_status(ErrorCode::invalid_request);
        }
        try {
            Lease lease{request->scope()};
            lease.deadline();
            Descriptor blocks{open(blocks_.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
            if (flock(blocks.fd, LOCK_SH | LOCK_NB) < 0) {
                throw std::runtime_error("Archive verification in progress");
            }
            bool started = false;
            auto serve = [&](int base) -> grpc::Status {
                Descriptor directory{
                    openat(base, request->block_id().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
                Descriptor manifest{
                    openat(directory.fd, "manifest.json", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
                struct stat metadata{};
                if (fstat(manifest.fd, &metadata) || !S_ISREG(metadata.st_mode) || metadata.st_size <= 0 ||
                    static_cast<std::uint64_t>(metadata.st_size) > archive::manifest_limit) {
                    throw std::runtime_error("Invalid block manifest");
                }
                std::string json(static_cast<std::size_t>(metadata.st_size), '\0');
                if (pread(manifest.fd, json.data(), json.size(), 0) != metadata.st_size) {
                    throw std::runtime_error("Unreadable block manifest");
                }
                const auto block = archive::Json::parse(json);
                archive::validate_manifest(block);
                using Time = google::protobuf::util::TimeUtil;
                const auto& scope = request->scope();
                const bool complete = !request->complete_block_manifest().empty();
                if ((complete && block.dump() != request->complete_block_manifest()) ||
                    block.at("site_id") != scope.point().site_id() ||
                    block.at("point_id") != scope.point().point_id() ||
                    block.at("block_id") != request->block_id() ||
                    block.at("media").at("sha256") != request->sha256() ||
                    block.at("begin_utc_us") != Time::TimestampToMicroseconds(scope.archive().start()) ||
                    block.at("end_utc_us") != Time::TimestampToMicroseconds(scope.archive().end())) {
                    return platform::rpc_status(ErrorCode::media_scope_denied);
                }
                Descriptor media{
                    openat(directory.fd, "media.mp4", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
                struct stat info{};
                if (fstat(media.fd, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0) {
                    throw std::runtime_error("Invalid media file");
                }
                auto layout = archive::verify_fragment_layout(media.fd, static_cast<std::uint64_t>(info.st_size));
                if (archive::digest_media(media.fd).sha256 != request->sha256()) {
                    throw std::runtime_error("Archive checksum mismatch");
                }
                std::shared_ptr<Descriptor> copy;
                const bool masked = !scope.privacy_regions().empty();
                if (masked && !scope.archive_h264())
                    return platform::rpc_status(ErrorCode::representation_unavailable);
                const bool converted =
                    scope.archive_h264() && (masked || block.at("media").at("tracks").at(0).at("codec") == "h265");
                if (complete && (masked || scope.archive_h264()))
                    return platform::rpc_status(ErrorCode::representation_unavailable);
                if (converted) {
                    copy = compatible_block(request->sha256(), media.fd,
                                            Time::TimestampToMicroseconds(scope.archive().end()) -
                                                Time::TimestampToMicroseconds(scope.archive().start()),
                                            scope.privacy_regions());
                    if (fstat(copy->fd, &info))
                        throw std::runtime_error("Cannot read converted block");
                    if (info.st_size <= 0)
                        throw std::runtime_error("Invalid converted block");
                    layout = archive::verify_fragment_layout(copy->fd, static_cast<std::uint64_t>(info.st_size));
                }
                auto offset = complete || request->initialization() ? 0 : layout.media_begin;
                const auto end = complete                    ? static_cast<std::uint64_t>(info.st_size)
                                 : request->initialization() ? layout.media_begin
                                                             : layout.media_end;
                v1::PartChunk chunk;
                chunk.set_total_bytes(end - offset);
                std::array<char, chunk_size> bytes{};
                while (offset < end && !context->IsCancelled()) {
                    const auto n =
                        pread(copy ? copy->fd : media.fd, bytes.data(),
                              std::min<std::uint64_t>(end - offset, bytes.size()), static_cast<off_t>(offset));
                    if (n <= 0) {
                        throw std::runtime_error("Archive read failed");
                    }
                    lease.deadline();
                    chunk.set_data(bytes.data(), static_cast<std::size_t>(n));
                    started = true;
                    if (!stream->Write(chunk)) {
                        break;
                    }
                    offset += static_cast<std::uint64_t>(n);
                }
                return grpc::Status::OK;
            };
            try {
                return serve(blocks.fd);
            } catch (const std::exception&) {
                if (started)
                    throw;
                if (!node_)
                    throw;
                auto path = archive::replica_blocks(replica_, blocks_.parent_path(),
                                                    {request->scope().point().site_id(), *node_});
                if (!path)
                    throw;
                Descriptor copies{open(path->c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
                lease.deadline();
                return serve(copies.fd);
            }
        } catch (const std::exception&) {
            return platform::rpc_status(ErrorCode::dependency_unavailable);
        }
    }

  private:
    struct CachedBlock {
        std::string hash, regions;
        std::int64_t duration;
        std::shared_ptr<Descriptor> file;
    };
    std::mutex conversion_;
    std::deque<CachedBlock> cache_;
    std::shared_ptr<Descriptor> compatible_block(const std::string& hash, int source, std::int64_t duration,
                                                 const std::string& regions)
    {
        std::scoped_lock guard{conversion_};
        for (const auto& entry : cache_)
            if (entry.hash == hash && entry.regions == regions && entry.duration == duration)
                return entry.file;
        auto out = std::make_shared<Descriptor>(memfd_create("bacuri-playback", MFD_CLOEXEC));
        const auto file = [](int fd) { return std::filesystem::path{"/proc/self/fd"} / std::to_string(fd); };
        const auto result = archive::select_frames(file(source), file(out->fd), 0, duration, true, regions);
        struct stat size{};
        if (!result.count || fstat(out->fd, &size) || size.st_size <= 0 || size.st_size > 32L * 1024 * 1024)
            throw std::runtime_error("Compatible block unavailable");
        if (cache_.size() >= 4)
            cache_.pop_front();
        cache_.push_back({hash, regions, duration, out});
        return out;
    }
    const std::filesystem::path blocks_{std::filesystem::path{platform::setting("BACURI_ARCHIVE_DIR")} / "blocks"};
    // The second archive copy serves a block that the primary volume cannot.
    const std::optional<std::filesystem::path> replica_{platform::optional_setting("BACURI_ARCHIVE_REPLICA_DIR")};
    const std::optional<std::string> node_{platform::optional_setting("BACURI_NODE_ID")};
};
} // namespace
} // namespace bacuri::vms::media

int main()
try {
    using namespace bacuri::vms;
    umask(0007);
    const auto metrics = observability::export_process_metrics(std::getenv("BACURI_METRICS_DIRECTORY"), "media");
    media::Delivery delivery;
    gst_init(nullptr, nullptr);
    media::LiveDelivery live;
    const auto path = platform::setting("BACURI_MEDIA_SOCKET");
    grpc::ServerBuilder builder;
    builder.AddListeningPort("unix:" + path, grpc::experimental::LocalServerCredentials(UDS));
    builder.RegisterService(&delivery);
    builder.RegisterService(&live);
    auto server = builder.BuildAndStart();
    if (!server || chmod(path.c_str(), 0660)) {
        throw std::runtime_error("Cannot open archive delivery socket");
    }
    boost::asio::io_context io;
    boost::asio::signal_set signals{io, SIGTERM, SIGINT};
    signals.async_wait(
        [&](auto, int) { server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds{2}); });
    io.run();
    server->Wait();
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
