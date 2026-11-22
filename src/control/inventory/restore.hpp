#pragma once
namespace bacuri::vms::control::inventory
{
inline constexpr char reset_recorder[] = "DELETE FROM bacuri_inventory.recorder_attempt;"
                                         "UPDATE bacuri_inventory.recorder_delivery SET decided_revision=0;"
                                         "UPDATE bacuri_inventory.configuration_progress SET "
                                         "applied_revision=NULL,application_state='pending',failure_code=NULL;";
}
