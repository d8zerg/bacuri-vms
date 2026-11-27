import { useT, useLanguage, type MessageKey } from "../../i18n";
import { nodeEn } from "../../i18n/node.en";
import { nodeRu } from "../../i18n/node.ru";
import { registerMessages } from "../../i18n/messages";
import { catalogMessage } from "../../adapters/errors";
import { Button } from "../../ui-kit/FormControls";
import { tasks, useNode, running, type Probe, type Settings } from "./useNode";
import "./node.css";
registerMessages(nodeRu, nodeEn);
const modes = ["cpu_only", "manual", "auto"] as const;

function reasonKey(reason: string): MessageKey {
  const key = "node." + reason;
  return Object.hasOwn(nodeRu, key)
    ? (key as keyof typeof nodeRu)
    : "node.reasonUnknown";
}
function useFormat() {
  const lang = useLanguage(),
    t = useT();
  return {
    percent: (ppm: number) =>
      new Intl.NumberFormat(lang, {
        style: "percent",
        maximumFractionDigits: 1,
      }).format(ppm / 1000000),
    memory: (bytes: number) =>
      new Intl.NumberFormat(lang, {
        style: "unit",
        unit: bytes >= 1e9 ? "gigabyte" : "megabyte",
        maximumFractionDigits: 1,
      }).format(bytes / (bytes >= 1e9 ? 1e9 : 1e6)),
    when: (date: string) =>
      new Intl.DateTimeFormat(lang, {
        dateStyle: "medium",
        timeStyle: "short",
      }).format(new Date(date)),
    reason: (value: string) => catalogMessage(value) ?? t(reasonKey(value)),
  };
}
type Model = ReturnType<typeof useNode>;
function ProbePanel({ model: m }: { model: Model }) {
  const t = useT(),
    fmt = useFormat(),
    p = m.probe;
  const active = running(p);
  const current =
    p?.job.steps.find((step) => step.state === "running") ??
    p?.job.steps.find((step) => step.state === "pending");
  return (
    <section className="na-panel" aria-labelledby="na-probe">
      <div className="na-heading">
        <h2 id="na-probe">{t("node.check")}</h2>
        <Button
          disabled={m.busy}
          onClick={() => void (active ? m.cancel() : m.check())}
        >
          {t(active ? "node.cancel" : p ? "node.again" : "node.start")}
        </Button>
      </div>
      {m.probeError && (
        <p className="na-warning" role="status">
          {t("node.unreachable")} {t(`error.${m.probeError}`)}
        </p>
      )}
      {p && (
        <>
          <p className="na-state" data-tone={p.job.state} role="status">
            {t(`node.${p.job.state}`)}
          </p>
          {(active ||
            p.job.state === "failed" ||
            p.job.state === "cancelled") && (
            <ol className="na-steps">
              {p.job.steps.map((s) => (
                <li key={s.name} data-state={s.state}>
                  <span>{t(`node.${s.name}`)}</span>
                  <span>
                    {t(
                      s.state === "completed"
                        ? "node.stageDone"
                        : s.state === "cancelled"
                          ? "node.stageCancelled"
                          : `node.${s.state}`,
                    )}
                  </span>
                  {active && current?.name === s.name && (
                    <div className="na-progress">
                      <progress
                        aria-label={t("node.progress")}
                        max={s.total || undefined}
                        value={s.total ? s.completed : undefined}
                      />
                      {s.total > 0 && (
                        <span>
                          {s.completed} / {s.total}
                        </span>
                      )}
                    </div>
                  )}
                </li>
              ))}
            </ol>
          )}
          {p.stale && (
            <p role="status" className="na-warning">
              {t("node.stale")}
            </p>
          )}
          {p.result && (
            <p className="na-meta">
              {t("node.last")} ·{" "}
              <time dateTime={p.result.measured_at}>
                {fmt.when(p.result.measured_at)}
              </time>
            </p>
          )}
        </>
      )}
    </section>
  );
}
function Hardware({ probe }: { probe: Probe | null }) {
  const t = useT(),
    fmt = useFormat();
  return (
    <section className="na-panel">
      <h2>{t("node.hardware")}</h2>
      {probe?.result?.devices.length ? (
        <ul className="na-hardware">
          {probe.result.devices.map((device) => (
            <li key={device.id}>
              {(device.backend !== "cpu" || device.model !== "CPU") && (
                <span className="na-chip">
                  {device.backend === "cpu" ? "CPU" : "GPU"}
                </span>
              )}
              <div>
                <strong>{device.model}</strong>
                {device.backend !== "cpu" && (
                  <small>
                    {device.backend === "vaapi" ? "VA-API" : "NVIDIA"}
                    {device.memory_bytes
                      ? " · " + fmt.memory(device.memory_bytes)
                      : ""}
                  </small>
                )}
                {device.backend !== "cpu" && (
                  <small>
                    {device.id} · {t("node.driver")} {device.driver}
                  </small>
                )}
              </div>
            </li>
          ))}
        </ul>
      ) : (
        <p className="na-meta">{t("node.noDevices")}</p>
      )}
    </section>
  );
}
function Budget({
  devices,
  name,
}: {
  devices: Settings["devices"];
  name: (id: string) => string;
}) {
  const t = useT(),
    fmt = useFormat();
  return (
    <section className="na-panel">
      <h2>{t("node.budget")}</h2>
      {devices.map((d) => (
        <div key={d.device_id} className="na-device-budget">
          <strong>{name(d.device_id)}</strong>
          <dl>
            {[
              [t("node.time"), fmt.percent(1000000 - d.time_ppm_used)],
              [
                t("node.sessions"),
                t("node.free", {
                  available: d.encoder_sessions - d.encoder_sessions_used,
                  total: d.encoder_sessions,
                }),
              ],
              [
                t("node.memory"),
                d.memory_bytes
                  ? t("node.free", {
                      available: fmt.memory(
                        d.memory_bytes - d.memory_bytes_used,
                      ),
                      total: fmt.memory(d.memory_bytes),
                    })
                  : t("node.unknown"),
              ],
            ].map(([label, value]) => (
              <div key={label}>
                <dt>{label}</dt>
                <dd>{value}</dd>
              </div>
            ))}
          </dl>
        </div>
      ))}
    </section>
  );
}
function Assignments({
  model: m,
  name,
}: {
  model: Model;
  name: (id: string) => string;
}) {
  const t = useT(),
    fmt = useFormat();
  const candidate = m.candidate;
  if (!candidate || candidate.mode === "cpu_only") return null;
  const plan = m.assessment ?? (!m.dirty && m.fresh ? m.saved : null);
  if (!m.verified || (!m.fresh && m.dirty)) return null;
  const live = m.probe?.scheduler?.assignments;
  return (
    <section className="na-panel na-assign">
      <h2>{t("node.tasks")}</h2>
      <p className="na-meta">{t("node.reference")}</p>
      <table>
        <thead>
          <tr>
            {["task", "estimate", "expected", "current"].map((k) => (
              <th key={k} scope="col">
                {t(reasonKey(k))}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {tasks.map((task) => {
            const row = plan?.decisions.find((r) => r.task === task);
            const options = (plan?.devices ?? [])
              .flatMap((d) =>
                (d.costs ?? [])
                  .filter((c) => c.task === task)
                  .map((c) => ({ ...c, id: d.device_id })),
              )
              .sort((a, b) => a.time_ppm - b.time_ppm);
            const cost =
              options.find((c) => c.id === row?.device_id) ?? options[0];
            const paths = live?.filter((r) => r.task === task);
            return (
              <tr
                key={task}
                data-rejected={plan?.rejected_tasks.includes(task) || undefined}
              >
                <th scope="row">
                  {candidate.mode === "manual" ? (
                    <label>
                      <input
                        type="checkbox"
                        checked={candidate.gpu_tasks?.includes(task) ?? false}
                        disabled={m.busy}
                        onChange={() => m.toggle(task)}
                      />
                      {t(`node.${task}`)}
                    </label>
                  ) : (
                    t(`node.${task}`)
                  )}
                </th>
                <td data-label={t("node.estimate")}>
                  {cost ? (
                    <>
                      <strong>
                        {cost.time_ppm > 1000000
                          ? "> " + fmt.percent(1000000)
                          : fmt.percent(cost.time_ppm)}
                      </strong>
                      <small>
                        {t("node.sessionCost", {
                          count: cost.encoder_sessions,
                        })}
                        {cost.memory_bytes
                          ? " · " + fmt.memory(cost.memory_bytes)
                          : ""}
                      </small>
                      {options.length > 1 && <small>{name(cost.id)}</small>}
                      {cost.exclusive && <small>{t("node.exclusive")}</small>}
                    </>
                  ) : (
                    t("node.unknown")
                  )}
                </td>
                <td data-label={t("node.expected")}>
                  {row ? (
                    <>
                      <strong>{name(row.device_id)}</strong>
                      <small>{fmt.reason(row.reason)}</small>
                    </>
                  ) : (
                    t("node.unknown")
                  )}
                </td>
                <td data-label={t("node.current")}>
                  {paths?.length
                    ? paths.map((p) => (
                        <div
                          key={JSON.stringify([
                            p.device_id,
                            p.reason,
                            p.releasing_device_id,
                          ])}
                        >
                          <strong>
                            {name(p.device_id)} × {p.count}
                          </strong>
                          <small>{fmt.reason(p.reason)}</small>
                          {p.releasing_device_id && (
                            <small>
                              {t("node.releasing", {
                                device: name(p.releasing_device_id),
                              })}
                            </small>
                          )}
                        </div>
                      ))
                    : t(live ? "node.idle" : "node.unknown")}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
      {plan?.rejected_tasks.length ? (
        <p role="status" className="na-warning">
          {t("node.rejected")} ·{" "}
          {plan.rejected_tasks.map((k) => t(`node.${k}`)).join(", ")}
        </p>
      ) : null}
      {plan?.issue && !plan.rejected_tasks.length && (
        <p role="status" className="na-warning">
          {fmt.reason(plan.issue)}
        </p>
      )}
      {!plan && !m.error && (
        <p role="status" className="na-meta">
          {t(m.fresh ? "node.pendingPreview" : "node.availableAfter")}
        </p>
      )}
    </section>
  );
}
export default function NodeAcceleration({
  site,
  node,
  csrf,
  expire,
}: {
  site: string;
  node: string;
  csrf: string;
  expire: () => void;
}) {
  const m = useNode(site, node, csrf, expire),
    t = useT(),
    fmt = useFormat();
  const hardware = m.probe?.result?.devices ?? [];
  let modeHint: MessageKey | undefined;
  if (m.candidate) {
    modeHint = `node.hint.${m.candidate.mode}`;
    if (!m.verified)
      modeHint =
        m.probe?.job.state === "failed" || m.probe?.job.state === "cancelled"
          ? "node.retryPreparation"
          : "node.preparing";
    else if (m.candidate.mode !== "cpu_only" && !m.fresh)
      modeHint = "node.availableAfter";
  }
  function name(id: string) {
    if (id === "cpu") return "CPU";
    if (id === "queue") return t("node.queue");
    const device = hardware.find((d) => d.id === id);
    if (!device) return id;
    return hardware.some((d) => d.id !== id && d.model === device.model)
      ? device.model + " · " + id
      : device.model;
  }
  return (
    <div className="na-page">
      <div className="na-title">
        <h1>{t("node.title")}</h1>
        {m.saved && (
          <span className="na-applied" role="status">
            {t("node.applied")} · <b>{t(`node.${m.saved.mode}`)}</b>
          </span>
        )}
      </div>
      {m.probe?.scheduler?.warnings.map((w) => (
        <div className="na-warning" role="status" key={w.device_id + w.reason}>
          <strong>{t("node.fallback")}</strong> ·{" "}
          {w.device_id && name(w.device_id) + " · "}
          {fmt.reason(w.reason)}
        </div>
      ))}
      <div className="na-layout">
        <div className="na-main">
          <section className="na-panel">
            <h2 id="na-mode">{t("node.mode")}</h2>
            {m.candidate ? (
              <>
                <div
                  className="na-modes"
                  role="radiogroup"
                  aria-labelledby="na-mode"
                >
                  {modes.map((mode) => (
                    <label key={mode}>
                      <input
                        type="radio"
                        name="na-mode"
                        value={mode}
                        checked={m.candidate?.mode === mode}
                        disabled={m.busy}
                        onChange={() => m.choose(mode)}
                      />
                      {t(`node.${mode}`)}
                    </label>
                  ))}
                </div>
                {modeHint && (
                  <p className="na-meta" role="status">
                    {t(modeHint)}
                  </p>
                )}
              </>
            ) : (
              !m.error && <p role="status">{t("loading.generic")}</p>
            )}
            <div className="na-actions">
              <Button
                variant="primary"
                disabled={!m.allowed}
                onClick={() => void m.apply()}
              >
                {t("node.apply")}
              </Button>
              {m.dirty && <span className="na-meta">{t("node.unsaved")}</span>}
            </div>
            {m.error && (
              <p className="na-warning" role="alert">
                {t(`error.${m.error}`)}{" "}
                <Button disabled={m.busy} onClick={m.reload}>
                  {t("site.retry")}
                </Button>
              </p>
            )}
          </section>
          <Assignments model={m} name={name} />
        </div>
        <aside className="na-side">
          <ProbePanel model={m} />
          <Hardware probe={m.probe} />
          {m.assessment?.devices.length ? (
            <Budget devices={m.assessment.devices} name={name} />
          ) : null}
        </aside>
      </div>
    </div>
  );
}
