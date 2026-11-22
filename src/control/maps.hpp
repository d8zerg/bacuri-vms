#include "control/access/session.hpp"
#pragma once

#include "control/api.hpp"
#include <stop_token>

namespace bacuri::vms::control
{
void maps(const Request&, Response&, const Sessions&, const std::string& site, std::string_view path,
          const std::string& origin);
namespace map_packages
{
void work(std::stop_token);
}
void import_map(const std::filesystem::path&);
struct MapImage {
    unsigned width, height;
    std::string png;
};
MapImage read_map_png(std::string_view bytes, bool tile);
} // namespace bacuri::vms::control
