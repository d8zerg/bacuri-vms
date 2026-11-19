#include "archive/mp4.hpp"
#include "control/support.hpp"
#include "media/lease.hpp"
#include "media/live.hpp"
#include <gst/gst.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <fcntl.h>
#include <google/protobuf/util/time_util.h>
#include <sys/stat.h>
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
        if (!request->scope().has_archive() || !control::canonical_id(request->block_id())) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid block identity"};
        }
        try {
            Lease lease{request->scope()};
            lease.deadline();
            Descriptor directory{
                openat(blocks_.fd, request->block_id().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
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
            if (block.at("site_id") != scope.point().site_id() ||
                block.at("point_id") != scope.point().point_id() || block.at("block_id") != request->block_id() ||
                block.at("media").at("sha256") != request->sha256() ||
                block.at("begin_utc_us") != Time::TimestampToMicroseconds(scope.archive().start()) ||
                block.at("end_utc_us") != Time::TimestampToMicroseconds(scope.archive().end())) {
                return {grpc::StatusCode::PERMISSION_DENIED, "Block outside authorized scope"};
            }
            Descriptor media{openat(directory.fd, "media.mp4", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
            struct stat info{};
            if (fstat(media.fd, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0) {
                throw std::runtime_error("Invalid media file");
            }
            const auto layout = archive::verify_fragment_layout(media.fd, info.st_size);
            if (archive::digest_media(media.fd).sha256 != request->sha256()) {
                throw std::runtime_error("Archive checksum mismatch");
            }
            auto offset = request->initialization() ? 0 : layout.media_begin;
            const auto end = request->initialization() ? layout.media_begin : layout.media_end;
            v1::PartChunk chunk;
            chunk.set_total_bytes(end - offset);
            std::array<char, chunk_size> bytes;
            while (offset < end && !context->IsCancelled()) {
                const auto n = pread(media.fd, bytes.data(), std::min<std::uint64_t>(end - offset, bytes.size()),
                                     static_cast<off_t>(offset));
                if (n <= 0) {
                    throw std::runtime_error("Archive read failed");
                }
                lease.deadline();
                chunk.set_data(bytes.data(), static_cast<std::size_t>(n));
                if (!stream->Write(chunk)) {
                    break;
                }
                offset += static_cast<std::uint64_t>(n);
            }
            return grpc::Status::OK;
        } catch (const std::exception&) {
            return {grpc::StatusCode::UNAVAILABLE, "Archive delivery unavailable"};
        }
    }

  private:
    Descriptor blocks_{open((std::filesystem::path{control::setting("BACURI_ARCHIVE_DIR")} / "blocks").c_str(),
                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
};
} // namespace
} // namespace bacuri::vms::media

int main()
try {
    using namespace bacuri::vms;
    umask(0007);
    media::Delivery delivery;
    gst_init(nullptr, nullptr);
    media::LiveDelivery live;
    const auto path = control::setting("BACURI_MEDIA_SOCKET");
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
