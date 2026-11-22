#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bacuri::vms::control
{
class Database;
}

namespace bacuri::vms::control::access
{
void lock_policy(Database&, const std::string& site, bool shared = false);
std::string scope_revision(Database&, const std::string& site);
enum class RoleLock { read, shared };
std::string site_role(Database&, const std::string& site, const std::string& user, RoleLock);
bool lock_point_grant(Database&, const std::string& site, const std::string& point, const std::string& user);
std::string role_for_existing_point(Database&, const std::string& site, const std::string& point,
                                    const std::string& user);
bool allowed(Database&, const std::string& site, const std::string& user, const std::string& point,
             const std::string& action, const std::string& surface = {}, const std::string& begin = {},
             const std::string& end = {});
std::vector<std::string> granted_point_ids(Database&, const std::string& site, const std::string& user,
                                           const std::string& action = "view", bool include_intervals = false);
std::vector<std::string> export_point_ids(Database&, const std::string& site, const std::string& user);
std::vector<std::string> viewer_ids(Database&, const std::string& site, const std::string& point);
std::string display_name(Database&, const std::string& user);
std::string browser_expiry(Database&, const std::string& browser);
std::string export_permission(Database&, const std::string& site, const std::string& point,
                              const std::string& user, const std::string& begin, const std::string& end);
struct ExportLease {
    std::string site, point;
    std::int64_t milliseconds;
};
std::optional<ExportLease> export_lease(Database&, const std::string& browser, const std::string& grant,
                                        const std::string& point, const std::string& begin,
                                        const std::string& end);

struct LocalAccount {
    std::string username, role;
};
struct GrantTarget {
    std::string site, username, point;
};
void provision_user(Database& db, const std::string& site, const LocalAccount& account,
                    const std::string& password, bool initial);

bool has_authenticator(Database& db, const std::string& username);

void revoke_export(Database& db, const GrantTarget& target);

void grant_export(Database& db, const GrantTarget& target, std::int64_t begin, std::int64_t end);

void revoke_point(Database& db, const GrantTarget& target);

void grant_point(Database& db, const GrantTarget& target);

std::optional<std::string> browser_point_expiry(Database& db, const std::string& browser, const std::string& site,
                                                const std::string& point, const std::string& surface,
                                                const std::string& begin, const std::string& end);

std::map<std::string, std::string> display_names(Database& db, const std::vector<std::string>& users);
} // namespace bacuri::vms::control::access
