// The access policy of a site as the access screen reads it, and the values its tabs share.
import type { FormEvent } from "react";
import type { components } from "../../../../../build/contracts/api";
import type { Point, Session } from "../../contracts/workspace";
import type { ListRefresh } from "../../ui-kit/FilterPanel";
import type { MessageKey, t } from "../../i18n";

export type Schema = components["schemas"];
export type User = Schema["AccessUser"];
export type Group = Schema["AccessGroup"];
export type Grant = Schema["AccessGrant"];
export const actions = {
  view: "access.action.view",
  audio: "access.action.audio",
  record: "access.action.record",
  audio_record: "access.action.audio_record",
  audio_talk: "access.action.audio_talk",
  ptz: "access.action.ptz",
  configure: "access.action.configure",
  protect: "access.action.protect",
  export: "access.action.export",
  export_masked: "access.action.export_masked",
  view_ptz: "access.action.view_ptz",
} as const satisfies Record<string, MessageKey>;
export const tabs = [
  ["users", "user-round"],
  ["groups", "users-round"],
  ["grants", "key-round"],
  ["privacy", "eye-off"],
  ["login", "log-in"],
  ["audit", "clipboard-list"],
] as const;
export type Tab = (typeof tabs)[number][0];
export interface Policy {
  revision: string;
  users: User[];
  groups: Group[];
  grants: Grant[];
  privacy: Schema["PrivacyEntry"][];
  provider: Schema["ProviderInfo"];
  audit: Schema["AccessAuditEvent"][];
  points: Point[];
}
export const field = (form: FormData, key: string) => {
  const value = form.get(key);
  return typeof value === "string" ? value : "";
};

// The values of the access screen that its tabs read and change.
export type AccessContext = {
  t: typeof t;
  site: string;
  session: Session;
  policy: Policy;
  save: (path: string, method: string, body?: unknown) => Promise<void>;
  form: (
    action: (form: FormData) => void,
  ) => (event: FormEvent<HTMLFormElement>) => void;
  refresh: ListRefresh;
  query: string;
  setQuery: (value: string) => void;
  resetFilters: () => void;
  pointQuery: string;
  setPointQuery: (value: string) => void;
  roleFilter: string;
  setRoleFilter: (value: string) => void;
  actionFilter: string;
  setActionFilter: (value: string) => void;
  group: string;
  setGroup: (value: string) => void;
  point: string;
  setPoint: (value: string) => void;
  users: User[];
  grants: Grant[];
  identities: User[];
  shownIdentities: User[];
  matchingGroups: Group[];
  matchingPoints: Point[];
  selected: Group | undefined;
  selectedPoint: string;
  privacy: Schema["PrivacyEntry"] | undefined;
  setMessage: (value: string) => void;
  busy: boolean;
};
