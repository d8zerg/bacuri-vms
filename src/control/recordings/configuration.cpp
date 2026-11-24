#include "control/recordings/configuration.hpp"
namespace bacuri::vms::control::recordings
{
void retention_configuration(ConfigurationSnapshot& snapshot)
{
    snapshot.section("bacuri_archive.archive_retention");
    snapshot.section("bacuri_archive.recording_policies");
    snapshot.section("bacuri_archive.archive_holds");
    snapshot.section("bacuri_archive.held_blocks");
    snapshot.section("bacuri_archive.investigations");
}
} // namespace bacuri::vms::control::recordings
