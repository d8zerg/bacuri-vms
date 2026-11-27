#pragma once
#include <gst/gst.h>
#include <stdexcept>
namespace bacuri::vms::acceleration
{
enum class CodecFault { none, codec, sessions, lost, reset };
class CodecFailure : public std::runtime_error
{
  public:
    explicit CodecFailure(const char* what, CodecFault cause = CodecFault::codec)
        : std::runtime_error{what}, fault{cause}
    {
    }
    CodecFault fault;
};
// Classify local driver diagnostics without forwarding plugin text or file locations to the journal.
[[noreturn]] void pipeline_error(GstMessage*);
} // namespace bacuri::vms::acceleration
