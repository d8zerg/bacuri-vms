#include "control/recordings/configuration.hpp"
namespace bacuri::vms::control::recordings
{
void retention_configuration(ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_archive.archive_retention");
    snapshot.section("bacuri_archive.recording_policies");
}
} // namespace bacuri::vms::control::recordings
