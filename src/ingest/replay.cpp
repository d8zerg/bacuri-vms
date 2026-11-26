#include "ingest/replay.hpp"
#include "ingest/rtsp_session.hpp"
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <poll.h>
#include <spawn.h>
#include <unistd.h>
#include <cerrno>
#include <span>
#include <utility>

namespace bacuri::vms::ingest
{
namespace
{
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
struct Socket {
    int fd;
    explicit Socket(int descriptor) : fd{descriptor} {}
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    ~Socket()
    {
        if (fd >= 0)
            close(fd);
    }
};
struct Actions {
    posix_spawn_file_actions_t value{};
    Actions()
    {
        if (posix_spawn_file_actions_init(&value))
            throw std::runtime_error("Replay spawn unavailable");
    }
    Actions(const Actions&) = delete;
    Actions& operator=(const Actions&) = delete;
    ~Actions()
    {
        posix_spawn_file_actions_destroy(&value);
    }
};
struct Worker {
    pid_t pid;
    explicit Worker(pid_t process) : pid{process} {}
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    ~Worker()
    {
        // A peer can stall GStreamer in RTSP teardown after all frames have been received.
        kill(pid, SIGKILL);
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
        }
    }
};
void send_bytes(int fd, const void* data, size_t length)
{
    auto* at = static_cast<const unsigned char*>(data);
    while (length) {
        auto n = send(fd, at, length, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            throw std::runtime_error("Replay channel closed");
        at += n;
        length -= static_cast<size_t>(n);
    }
}
bool read_bytes(int fd, void* data, size_t length, Clock::time_point deadline,
                const std::function<bool()>& cancelled)
{
    auto* at = static_cast<unsigned char*>(data);
    while (length && Clock::now() < deadline && !cancelled()) {
        pollfd event{fd, POLLIN, 0};
        auto ready = poll(&event, 1, 100);
        if (ready == 0 || (ready < 0 && errno == EINTR))
            continue;
        if (ready < 0)
            return false;
        auto n = recv(fd, at, length, MSG_DONTWAIT);
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (n <= 0)
            return false;
        at += n;
        length -= static_cast<size_t>(n);
    }
    return length == 0;
}
// Private host-native protocol between two instances of the same recorder executable.
struct Sample {
    std::uint64_t length, ntp, duration, received;
    std::int32_t width, height;
    std::uint32_t flags, reserved;
};
} // namespace
int replay_worker()
{
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0)
        return 1;
    const auto deadline = Clock::now() + 5s;
    const auto active = [] { return false; };
    std::uint32_t size{};
    if (!read_bytes(0, &size, sizeof size, deadline, active) || !size || size > 65536)
        return 1;
    std::string json(size, '\0');
    if (!read_bytes(0, json.data(), size, deadline, active))
        return 1;
    const auto q = nlohmann::json::parse(json);
    if (q.at("parent") != getppid())
        return 1;
    Endpoint endpoint{q.at("url"), q.at("username"), q.at("password")};
    receive_rtsp(
        endpoint, {},
        [](const FramePtr& f, VideoFormat format, bool discontinuity) {
            Sample sample{f->access_unit.size(),
                          f->ntp_ns.value_or(0),
                          f->duration_ns.value_or(0),
                          static_cast<std::uint64_t>(f->time.received_at().time_since_epoch().count()),
                          format.width,
                          format.height,
                          unsigned(f->keyframe) | (unsigned(discontinuity) << 1) |
                              (unsigned(f->codec == VideoCodec::h265) << 2),
                          0};
            send_bytes(0, &sample, sizeof sample);
            send_bytes(0, f->access_unit.data(), f->access_unit.size());
        },
        ReplayWindow{q.at("begin"), q.at("end")}, [] { shutdown(0, SHUT_WR); });
    return 0;
}
std::string receive_replay(const Endpoint& endpoint, ReplayInterval interval,
                           const std::function<bool()>& cancelled,
                           const std::function<void(FramePtr, VideoFormat, bool)>& receive)
{
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair))
        throw std::runtime_error("Replay channel unavailable");
    Socket parent{pair[0]}, child{pair[1]};
    Actions actions;
    if (posix_spawn_file_actions_adddup2(&actions.value, child.fd, 0) ||
        posix_spawn_file_actions_addclosefrom_np(&actions.value, 3))
        throw std::runtime_error("Replay spawn unavailable");
    char executable[] = "/proc/self/exe", command[] = "receive-replay";
    char* args[] = {executable, command, nullptr};
    pid_t pid{};
    if (posix_spawn(&pid, executable, &actions.value, nullptr, args, environ))
        throw std::runtime_error("Replay spawn unavailable");
    Worker worker{pid};
    close(std::exchange(child.fd, -1));
    const auto request = nlohmann::json{
        {"parent", getpid()},
        {"url", endpoint.url},
        {"username", endpoint.username},
        {"password", endpoint.password},
        {"begin", interval.begin_us},
        {"end",
         interval.end_us}}.dump();
    if (request.size() > 65536)
        throw std::invalid_argument("Replay request too large");
    const auto size = static_cast<std::uint32_t>(request.size());
    send_bytes(parent.fd, &size, sizeof size);
    send_bytes(parent.fd, request.data(), size);
    const auto deadline = Clock::now() + 30s;
    while (!cancelled()) {
        Sample s{};
        if (!read_bytes(parent.fd, &s, sizeof s, deadline, cancelled))
            break;
        if (!s.length || s.length > limits(Profile::main).bytes || s.flags > 7)
            throw std::runtime_error("Invalid replay sample");
        auto time = domain::ProjectedTime::from_reception(domain::UtcTime{domain::Microseconds{s.received}});
        auto f = std::make_shared<Frame>(Frame{std::vector<std::byte>(s.length),
                                               time,
                                               {},
                                               s.ntp ? std::optional{s.ntp} : std::nullopt,
                                               {},
                                               bool(s.flags & 1),
                                               s.duration ? std::optional{s.duration} : std::nullopt,
                                               (s.flags & 4) ? VideoCodec::h265 : VideoCodec::h264});
        if (!read_bytes(parent.fd, f->access_unit.data(), s.length, deadline, cancelled))
            break;
        receive(f, {s.width, s.height}, bool(s.flags & 2));
    }
    return Clock::now() >= deadline ? "frame_timeout" : "stopped";
}
} // namespace bacuri::vms::ingest
