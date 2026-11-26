#pragma once
#include "application/control_api/http.hpp"
#include <map>
#include <vector>
namespace bacuri::vms::postgresql
{
class ConfigurationSnapshot;
class QuerySession;
} // namespace bacuri::vms::postgresql

namespace bacuri::vms::control
{
class Sessions;
namespace access
{
struct MaterialAccess;
}
namespace investigations
{
void case_configuration(postgresql::ConfigurationSnapshot&);
struct CaseBookmark {
    std::string at, name;
};
struct CaseInterval {
    std::string start, end;
};
struct CaseMaterial {
    std::string point_id, hold_id;
    CaseInterval interval;
    std::vector<CaseBookmark> bookmarks;
};
struct ExportCase {
    std::string id, number, assignee, notes, revision;
    std::vector<CaseMaterial> materials;
};
struct CaseMaterialInput {
    std::string point_id;
    CaseInterval interval;
    std::vector<CaseBookmark> bookmarks;
};
struct CaseUpdate {
    std::string id, number, assignee, notes, revision;
    std::vector<CaseMaterialInput> materials;
    std::string reason;
    std::int64_t expires_us;
};
std::string save_case(postgresql::QuerySession&, const std::string& site, const std::string& actor,
                      const std::string& role, CaseUpdate update, const ExportCase* previous);
ExportCase case_for_export(postgresql::QuerySession&, const std::string& site, const std::string& id,
                           const std::string& user);
void investigation(const Request&, Response&, const Sessions&, const std::string& site, const std::string& id);
std::map<std::string, std::string> eligible_recipients(postgresql::QuerySession&, const std::string& site,
                                                       const std::string& actor,
                                                       const std::vector<access::MaterialAccess>& spans);
void case_recipients(const Request&, Response&, const Sessions&, const std::string& site);
} // namespace investigations
} // namespace bacuri::vms::control
