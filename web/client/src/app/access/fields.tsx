// Form fields shared by the access tabs.
import { useT } from "../../i18n";
import type { User } from "./model";

export function Roles({ value = "operator" }: { value?: string }) {
  const t = useT();
  return (
    <label>
      {t("access.role")}
      <select name="role" defaultValue={value}>
        <option value="operator">{t("access.role.operator")}</option>
        <option value="administrator">{t("access.role.administrator")}</option>
      </select>
    </label>
  );
}
export function People({ users }: { users: User[] }) {
  const t = useT();
  return (
    <label>
      {t("access.user")}
      <select required name="user">
        {users.map((u) => (
          <option value={u.id} key={u.id}>
            {u.username}
          </option>
        ))}
      </select>
    </label>
  );
}
