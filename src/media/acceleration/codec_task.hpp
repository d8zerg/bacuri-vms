#pragma once
#include "media/acceleration/codec_error.hpp"
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace bacuri::vms::acceleration
{
struct CodecPath {
    std::string device{"cpu"}, backend{"cpu"}, grant;
    std::map<std::string, std::string> factories;
    std::string decoder(const std::string& codec) const;
    std::string encoder(bool live) const;
};
class Reassigned : public std::runtime_error
{
  public:
    Reassigned() : std::runtime_error{"Codec assignment changed"} {}
};
// Owned by the worker. Destroy its codecs before acknowledging release or closing the task.
class CodecTask
{
  public:
    explicit CodecTask(const std::string& kind);
    ~CodecTask();
    CodecTask(const CodecTask&) = delete;
    CodecTask& operator=(const CodecTask&) = delete;
    const CodecPath& poll();
    void check(const CodecPath&);
    void released(const CodecPath&, CodecFault fault, bool stay_on_cpu = false);
    void used(const CodecPath&);
    void next_checkpoint();

  private:
    struct State;
    std::unique_ptr<State> state_;
};
// A complete archive block is committed by the caller only after the transform succeeds.
template <class F> auto process_block(CodecTask& task, F operation)
{
    const auto path = task.poll();
    try {
        return operation(path);
    } catch (const Reassigned&) {
        task.released(path, CodecFault::none, true);
    } catch (const CodecFailure& error) {
        if (path.grant.empty())
            throw;
        task.released(path, error.fault, true);
    }
    const auto fallback = task.poll();
    return operation(fallback);
}
} // namespace bacuri::vms::acceleration
