#include "media/acceleration/probe.hpp"
#include "platform/scope_exit.hpp"
#include <glib.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <csignal>

extern char** environ;
namespace bacuri::vms::acceleration
{
using namespace std::chrono_literals;

Json subprocess(const Json& request, const Interrupt& interrupted)
{
    auto close_file = [](FILE* f) { std::fclose(f); };
    std::unique_ptr<FILE, decltype(close_file)> output{std::tmpfile(), close_file};
    if (!output)
        throw std::runtime_error("Probe output unavailable");
    posix_spawn_file_actions_t actions{};
    if (posix_spawn_file_actions_init(&actions))
        throw std::runtime_error("Probe spawn unavailable");
    platform::ScopeExit cleanup{[&actions] { posix_spawn_file_actions_destroy(&actions); }};
    if (posix_spawn_file_actions_adddup2(&actions, fileno(output.get()), STDOUT_FILENO) ||
        posix_spawn_file_actions_addclosefrom_np(&actions, 3))
        throw std::runtime_error("Probe output redirect failed");
    std::string executable{"/proc/self/exe"}, option{"--acceleration-sample"}, input{request.dump()};
    std::array<char*, 4> args{executable.data(), option.data(), input.data(), nullptr};
    pid_t child{};
    if (posix_spawn(&child, executable.c_str(), &actions, nullptr, args.data(), environ))
        throw std::runtime_error("Cannot start probe");
    const auto until = std::chrono::steady_clock::now() + 12s;
    int status{};
    for (;;) {
        const auto done = waitpid(child, &status, WNOHANG);
        if (done == child)
            break;
        if (done < 0 && errno != EINTR)
            throw std::runtime_error("Cannot wait for probe");
        if (interrupted() || std::chrono::steady_clock::now() >= until) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            return {{"state", "interrupted_or_timeout"}};
        }
        std::this_thread::sleep_for(20ms);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return {{"state", "failed"}};
    const auto size = ftell(output.get());
    if (size < 1 || size > 65536 || fseek(output.get(), 0, SEEK_SET))
        return {{"state", "failed"}};
    std::string data(static_cast<std::size_t>(size), '\0');
    if (fread(data.data(), 1, data.size(), output.get()) != data.size())
        return {{"state", "failed"}};
    return Json::parse(data);
}

int worker(const char* input)
{
    // A slow driver is isolated from the media service. The parent enforces the wall-clock deadline.
    alarm(15);
    static_cast<void>(nice(15));
    const rlimit cpu{10, 11};
    setrlimit(RLIMIT_CPU, &cpu);
    const auto request = Json::parse(input);
    const auto stage = request.at("stage").get<std::string>();
    const auto output = stage == "inventory"   ? inventory()
                        : stage == "inference" ? measure_inference(request)
                                               : measure_video(request);
    std::puts(output.dump().c_str());
    return 0;
}
} // namespace bacuri::vms::acceleration
