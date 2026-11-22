import type {
  AlertWork,
  AuditEntry,
  Services,
} from "../src/contracts/workspace";
type Methods = Pick<
  Services,
  "alertWork" | "alertCommand" | "audit" | "delegate" | "revokeDelegation"
>;
export function alertWorkFixture(point: string, utc: string): Methods {
  const cards = new Map<string, AlertWork>();
  const grants: AlertWork["delegations"] = [];
  const entries: AuditEntry[] = [];
  function card(alert: string): AlertWork {
    const existing = cards.get(alert);
    if (existing) return existing;
    const fresh: AlertWork = {
      alert_id: alert,
      point_id: point,
      state: "new",
      version: 0,
      owner: null,
      result: "",
      actions: ["claim"],
      history: [],
      delegations: grants,
    };
    cards.set(alert, fresh);
    return fresh;
  }
  return {
    alertWork: (_site, alert) => Promise.resolve(structuredClone(card(alert))),
    alertCommand: (_site, alert, body, key) => {
      const work = card(alert),
        from = work.state;
      work.owner = "Дежурный";
      work.version++;
      work.state = body.action === "close" ? "closed" : "in_progress";
      if (body.action === "record_result") work.result = body.reason;
      work.actions =
        work.state === "closed"
          ? []
          : work.result
            ? ["record_result", "close"]
            : ["record_result"];
      const entry: AuditEntry = {
        id: key,
        point_id: point,
        alert_id: alert,
        version: work.version,
        action: body.action,
        from_state: from,
        to_state: work.state,
        reason: body.reason,
        actor_kind: "user",
        initiator: "Дежурный",
        initiator_id: "installer",
        owner: work.owner,
        recorded_at: utc,
        service: null,
        delegation_id: null,
        delegated_action: null,
        expires_at: null,
        outcome: "applied",
      };
      work.history.unshift(entry);
      entries.unshift(entry);
      return Promise.resolve({ event_id: key, version: work.version });
    },
    audit: (_site, filter) => {
      const matching = entries.filter(
        (entry) =>
          (!filter.alert_id || filter.alert_id === entry.alert_id) &&
          entry.reason.includes(filter.query ?? ""),
      );
      const start = filter.before
        ? matching.findIndex((entry) => entry.id === filter.before) + 1
        : 0;
      const items = matching.slice(start, start + 2);
      return Promise.resolve({
        items,
        ...(start + items.length < matching.length
          ? { next_cursor: items[items.length - 1]!.id }
          : {}),
      });
    },
    delegate: (_site, body) => {
      const grant = {
        id: "delegated",
        service: body.service,
        action: body.action,
        expires_at: utc,
      };
      grants.push(grant);
      return Promise.resolve({ ...grant, token: "test-credential" });
    },
    revokeDelegation: () => {
      grants.splice(0);
      return Promise.resolve();
    },
  };
}
