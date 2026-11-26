#pragma once
#include "application/control_api/playback_time.hpp"
namespace bacuri::vms::control::recordings
{
bool can_view(postgresql::QuerySession&, const std::string& site, const std::string& user,
              const std::string& point, Window span);
void lock_archive(postgresql::Database&, const std::string& site);
void protect(postgresql::Database&, const std::string& site, const std::string& actor, Window span,
             const std::string& owner, const std::string& point, const std::string& id, const std::string& reason,
             std::int64_t expiry);
void release_hold(postgresql::Database&, const std::string& site, const std::string& id);
std::int64_t expires(const Json& body);
std::string reason_field(const Json& body);
} // namespace bacuri::vms::control::recordings
