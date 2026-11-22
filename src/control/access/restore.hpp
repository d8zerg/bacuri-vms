#pragma once
namespace bacuri::vms::control::access
{
inline constexpr char restore_search_path[] = "SET LOCAL search_path=pg_catalog,bacuri_access,pg_temp;";
inline constexpr char legacy_restore_search_path[] = "SET LOCAL search_path=pg_catalog,bacuri_inventory,pg_temp;";
inline constexpr char reset_sessions[] = "DELETE FROM webauthn_credentials;"
                                         "DELETE FROM webauthn_challenges;"
                                         "DELETE FROM webauthn_enrollment;"
                                         "DELETE FROM browser_sessions;";
inline constexpr char audit_restore[] = "INSERT INTO credential_audit(user_id,action) SELECT DISTINCT "
                                        "user_id,'restore_reset' FROM site_roles WHERE role='administrator';";
} // namespace bacuri::vms::control::access
