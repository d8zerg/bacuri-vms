#pragma once

#include "control/api.hpp"

namespace bacuri::vms::control
{
void maps(const Request&, Response&, const Sessions&, const std::string& site, std::string_view path,
          const std::string& origin);
void import_map(const std::filesystem::path&);
struct MapImage {
    unsigned width, height;
    std::string png;
};
MapImage read_map_png(std::string_view bytes, bool tile);
} // namespace bacuri::vms::control
