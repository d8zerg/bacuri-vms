#include "maintenance.hpp"
#include <fstream>
#include <stdexcept>
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace bacuri::vms::maintenance
{
void ensure(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}
Temp::Temp(const fs::path& parent)
{
    auto name = (parent / "bacuri-maintain-XXXXXX").string();
    ensure(mkdtemp(name.data()), "Cannot create private staging directory");
    dir = name;
}
Temp::~Temp()
{
    std::error_code ec;
    fs::remove_all(dir, ec);
}
std::string read(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    ensure(f.is_open(), "Cannot read file");
    return {std::istreambuf_iterator<char>(f), {}};
}
void put(const fs::path& path, const std::string& value)
{
    std::ofstream f;
    f.exceptions(std::ios::failbit | std::ios::badbit);
    f.open(path, std::ios::binary);
    f << value;
    f.close();
}
int run(Args args, const Io& io)
{
    std::vector<char*> words;
    for (auto& a : args)
        words.push_back(a.data());
    words.push_back(nullptr);
    auto pid = fork();
    ensure(pid >= 0, "Cannot fork command");
    if (!pid) {
        if (!io.cwd.empty() && chdir(io.cwd.c_str()) != 0)
            _exit(126);
        if (!io.input.empty()) {
            int f = open(io.input.c_str(), O_RDONLY | O_NOFOLLOW);
            if (f < 0 || dup2(f, 0) < 0)
                _exit(126);
            close(f);
        }
        if (!io.output.empty()) {
            int f = open(io.output.c_str(), O_WRONLY | O_NOFOLLOW | O_CREAT | O_TRUNC, 0600);
            if (f < 0 || dup2(f, 1) < 0)
                _exit(126);
            close(f);
        }
        execv(words.front(), words.data());
        _exit(127);
    }
    int s{};
    while (waitpid(pid, &s, 0) < 0) {
        if (errno != EINTR)
            throw std::runtime_error("Cannot wait for command");
    }
    int result = WIFEXITED(s) ? WEXITSTATUS(s) : 128;
    if (result && !io.allow_failure)
        throw std::runtime_error("Command failed: " + args.front());
    return result;
}
std::string capture(Args args, bool optional)
{
    Temp tmp;
    auto file = tmp.dir / "out";
    if (run(std::move(args), {.output = file, .allow_failure = optional}))
        return {};
    auto text = read(file);
    while (text.ends_with('\n'))
        text.pop_back();
    return text;
}
} // namespace bacuri::vms::maintenance
