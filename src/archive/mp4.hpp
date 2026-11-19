#pragma once
#include <cstdint>
namespace bacuri::vms::archive
{
void verify_fragment_layout(int fd, std::uint64_t size);
}
