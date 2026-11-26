#include "modules/recordings/configuration.hpp"
namespace bacuri::vms::control::recordings
{
void retention_configuration(postgresql::ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_archive.archive_retention");
    snapshot.section("bacuri_archive.recording_policies");
    snapshot.section("bacuri_archive.archive_holds");
    snapshot.section("bacuri_archive.held_blocks");
}
} // namespace bacuri::vms::control::recordings
