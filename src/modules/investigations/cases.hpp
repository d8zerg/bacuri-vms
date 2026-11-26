#pragma once
#include "application/control_api/api.hpp"
namespace bacuri::vms::postgresql
{
class ConfigurationSnapshot;
}

namespace bacuri::vms::control
{
class Sessions;
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
ExportCase case_for_export(postgresql::QuerySession&, const std::string& site, const std::string& id,
                           const std::string& user);
void investigation(const Request&, Response&, const Sessions&, const std::string& site, const std::string& id);
void case_recipients(const Request&, Response&, const Sessions&, const std::string& site);
} // namespace investigations
} // namespace bacuri::vms::control
