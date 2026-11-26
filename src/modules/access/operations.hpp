#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bacuri::vms::postgresql
{
class Database;
class QuerySession;
} // namespace bacuri::vms::postgresql

namespace bacuri::vms::control::access
{
std::map<std::string, std::vector<std::string>> group_memberships(postgresql::Database&, const std::string& site);
void lock_policy(postgresql::Database&, const std::string& site, bool shared = false);
std::string scope_revision(postgresql::Database&, const std::string& site);
enum class RoleLock { read, shared };
std::string site_role(postgresql::QuerySession&, const std::string& site, const std::string& user, RoleLock);
bool lock_point_grant(postgresql::Database&, const std::string& site, const std::string& point,
                      const std::string& user);
std::string role_for_existing_point(postgresql::Database&, const std::string& site, const std::string& point,
                                    const std::string& user);
std::string delivery_regions(postgresql::QuerySession&, const std::string& site, const std::string& point,
                             const std::string& surface);
bool allowed(postgresql::QuerySession&, const std::string& site, const std::string& user, const std::string& point,
             const std::string& action, const std::string& surface = {}, const std::string& begin = {},
             const std::string& end = {});
std::vector<std::string> granted_point_ids(postgresql::Database&, const std::string& site, const std::string& user,
                                           const std::string& action = "view", bool include_intervals = false);
std::vector<std::string> export_point_ids(postgresql::Database&, const std::string& site, const std::string& user);
std::vector<std::string> viewer_ids(postgresql::Database&, const std::string& site, const std::string& point);
std::map<std::string, std::string> investigation_assignees(postgresql::Database&, const std::string& site,
                                                           const std::string& spans);
std::string display_name(postgresql::Database&, const std::string& user);
std::string browser_expiry(postgresql::Database&, const std::string& browser);
std::string export_regions(postgresql::QuerySession&, const std::string& site, const std::string& point);
std::string export_permission(postgresql::QuerySession&, const std::string& site, const std::string& point,
                              const std::string& user, const std::string& begin, const std::string& end,
                              bool derivative = false);
struct ExportLease {
    std::string site, point;
    std::int64_t milliseconds;
};
std::optional<ExportLease> export_lease(postgresql::Database&, const std::string& browser,
                                        const std::string& grant, const std::string& point,
                                        const std::string& begin, const std::string& end, bool derivative = false,
                                        const std::string& regions = {}, bool sound = false);

struct LocalAccount {
    std::string username, role;
};
struct GrantTarget {
    std::string site, username, point;
};
void provision_user(postgresql::Database& db, const std::string& site, const LocalAccount& account,
                    const std::string& password, bool initial);

bool has_authenticator(postgresql::Database& db, const std::string& username);

void revoke_export(postgresql::Database& db, const GrantTarget& target);

void grant_export(postgresql::Database& db, const GrantTarget& target, std::int64_t begin, std::int64_t end);

void revoke_point(postgresql::Database& db, const GrantTarget& target);

void grant_point(postgresql::Database& db, const GrantTarget& target);

std::optional<std::string> browser_point_expiry(postgresql::Database& db, const std::string& browser,
                                                const std::string& site, const std::string& point,
                                                const std::string& surface, const std::string& begin,
                                                const std::string& end);

std::map<std::string, std::string> display_names(postgresql::Database& db, const std::vector<std::string>& users);
} // namespace bacuri::vms::control::access
