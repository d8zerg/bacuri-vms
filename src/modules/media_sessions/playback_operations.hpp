#pragma once
#include "application/control_api/api.hpp"

namespace bacuri::vms::control::playback
{
struct PointAccess {
    std::string site, point, browser, role, user;
};
Json search(postgresql::Database& db, const Key& key, const PointAccess& access, Json params,
            std::optional<std::int64_t> binding = {});
Json create(postgresql::Database& db, const Key& key, const PointAccess& access, const Request& req);
} // namespace bacuri::vms::control::playback
