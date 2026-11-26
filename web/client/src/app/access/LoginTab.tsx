// The corporate identity provider and the accounts linked to it.
import { Button } from "../../ui-kit/FormControls";
import { FilterPanel } from "../../ui-kit/FilterPanel";

import { field, type AccessContext } from "./model";
import { People } from "./fields";

export function LoginTab({ ctx }: { ctx: AccessContext }) {
  const { t, policy, save, form, refresh, query, setQuery, shownIdentities } =
    ctx;
  return (
    <>
      <FilterPanel
        refresh={refresh}
        label={t("access.identityFilters")}
        queryLabel={t("access.identityQuery")}
        query={query}
        onQuery={setQuery}
        onReset={() => setQuery("")}
      />
      {!shownIdentities?.length && (
        <p role="status">
          {query ? t("access.noIdentitiesFound") : t("access.noIdentities")}
        </p>
      )}
      <ul>
        {shownIdentities?.map((u) => (
          <li key={u.id}>
            {u.username} · {u.issuer} · {u.subject}{" "}
            <Button
              onClick={() =>
                void save("users/" + u.id + "/external-identity", "DELETE")
              }
            >
              {t("access.unlink")}
            </Button>
          </li>
        ))}
      </ul>
      <form
        onSubmit={form((f) => {
          const secret = field(f, "secret");
          void save("identity/provider", "PUT", {
            enabled: f.has("enabled"),
            issuer: field(f, "issuer"),
            client_id: field(f, "client"),
            admin_acr: field(f, "acr"),
            ...(secret ? { client_secret: secret } : {}),
          });
        })}
      >
        <h2>OpenID Connect</h2>
        <label>
          <input
            type="checkbox"
            name="enabled"
            defaultChecked={policy.provider.enabled}
          />
          {t("access.keycloak")}
        </label>
        <label>
          Issuer HTTPS
          <input
            name="issuer"
            type="url"
            required
            defaultValue={policy.provider.issuer}
          />
        </label>
        <label>
          Client ID
          <input
            name="client"
            required
            defaultValue={policy.provider.client_id}
          />
        </label>
        <label>
          {t("access.clientSecret")}
          <input
            type="password"
            autoComplete="new-password"
            name="secret"
            required={!policy.provider.issuer}
          />
        </label>
        <label>
          {t("access.mfa")}
          <input required name="acr" defaultValue={policy.provider.admin_acr} />
        </label>
        <Button type="submit">{t("access.saveProvider")}</Button>
      </form>
      <form
        onSubmit={form(
          (f) =>
            void save(
              "users/" + field(f, "user") + "/external-identity",
              "PUT",
              {
                issuer: policy.provider.issuer,
                subject: field(f, "subject"),
              },
            ),
        )}
      >
        <h2>{t("access.externalAccount")}</h2>
        <People users={policy.users} />
        <label>
          {t("access.subject")}
          <input required name="subject" />
        </label>
        <Button type="submit" disabled={!policy.provider.issuer}>
          {t("access.link")}
        </Button>
      </form>
    </>
  );
}
