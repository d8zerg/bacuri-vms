import { useEffect, useRef, useState } from "react";
import type {
  CapturePolicy,
  CapturePolicies,
  CaptureRule,
  CaptureStorage,
  Point,
  Services,
} from "../contracts/workspace";
import { Button, ControlGlyph, IconButton } from "../ui-kit/FormControls";
import {
  FilterPanel,
  FilterSegments,
  matchesQuery,
  needsListSearch,
} from "../ui-kit/FilterPanel";
import "./recording-policies.css";
const week = ["Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"];
type Period = { days: number[]; start: number; end: number };
function groupPeriods(schedule: CaptureRule["schedule"]): Period[] {
  const periods: Period[] = [];
  for (const { day, start, end } of schedule) {
    const period = periods.find((p) => p.start === start && p.end === end);
    if (period) {
      if (!period.days.includes(day)) period.days.push(day);
    } else periods.push({ days: [day], start, end });
  }
  return periods;
}
const defaults: CaptureRule = {
  continuous: true,
  manual: false,
  event: false,
  zone: "UTC",
  schedule: [],
  pre: 5,
  post: 10,
  audio: false,
  days: null,
};
const time = (minute: number) =>
  String(Math.floor(minute / 60) % 24).padStart(2, "0") +
  ":" +
  String(minute % 60).padStart(2, "0");
const minute = (value: string) => {
  const [h = 0, m = 0] = value.split(":").map(Number);
  return h * 60 + m;
};
function archiveDepth(oldest: number, newest: number) {
  const seconds = Math.max(0, (newest - oldest) / 1e6);
  const [value, unit] =
    seconds < 60
      ? [seconds, "с"]
      : seconds < 3600
        ? [seconds / 60, "мин"]
        : seconds < 86400
          ? [seconds / 3600, "ч"]
          : [seconds / 86400, "дн."];
  return (
    Number(value).toLocaleString("ru", { maximumFractionDigits: 1 }) +
    " " +
    unit
  );
}
function modes(rule: CaptureRule) {
  return (
    [
      rule.continuous && "Непрерывно",
      rule.manual && "Вручную",
      rule.event && "По движению",
      rule.schedule.length > 0 && "По расписанию",
    ]
      .filter(Boolean)
      .join(" · ") || "Запись отключена"
  );
}
const recordingStates: Record<string, string> = {
  unbound: "Камера не привязана",
  source_offline: "Нет основного потока",
  awaiting_policy: "Ожидание политики",
  selected: "Запись по политике",
  waiting_event: "Ожидание движения",
  outside_schedule: "Вне расписания",
  waiting_manual: "Ожидание ручного запуска",
  disabled: "Запись отключена",
  recording_stopped: "Достигнут резерв тома",
  offline: "Рекордер недоступен",
};
type CaptureGroup = Awaited<ReturnType<Services["captureGroups"]>>[number];

function CameraSummary({
  point,
  policy,
  group,
  storage,
  edit,
  openGroup,
}: {
  point: Pick<Point, "id" | "name">;
  policy: CapturePolicy | undefined;
  group: CaptureGroup | undefined;
  storage: CaptureStorage;
  edit: () => void;
  openGroup: (id: string) => void;
}) {
  const rule = policy?.rule ?? defaults;
  const volume = storage.volume;
  const state = !volume
    ? "offline"
    : volume.state === "recording_stopped"
      ? volume.state
      : volume.point_states?.[point.id];
  const depth = storage.points.find((row) => row.point === point.id);
  const days =
    rule.days === null
      ? storage.days
      : Math.min(rule.days, storage.days ?? rule.days);
  return (
    <section
      className="rp-camera-summary"
      aria-label={"Настройки записи камеры «" + point.name + "»"}
    >
      <div className="rp-origin">
        <strong tabIndex={-1} data-policy-focus>
          {point.name}
        </strong>
        {state && <span>{recordingStates[state]}</span>}
      </div>
      <dl className="rp-facts">
        <div>
          <dt>Источник настроек</dt>
          <dd>
            {group ? (
              <Button
                className="rp-group-link"
                onClick={() => openGroup(group.id)}
                aria-label={"Открыть политику группы «" + group.name + "»"}
              >
                <ControlGlyph icon="users-round" />
                {group.name}
                <ControlGlyph icon="chevron-right" />
              </Button>
            ) : policy?.scope === "point" ? (
              "Индивидуальная политика"
            ) : (
              "Настройки площадки"
            )}
          </dd>
        </div>
        <div>
          <dt>Режим записи</dt>
          <dd>{modes(rule)}</dd>
        </div>
        {rule.event && (
          <div>
            <dt>По движению</dt>
            <dd>
              {rule.pre} с до события · {rule.post} с после
            </dd>
          </div>
        )}
        {rule.schedule.length > 0 && (
          <div>
            <dt>Расписание · {rule.zone}</dt>
            <dd>
              {groupPeriods(rule.schedule).map((p) => (
                <span className="rp-summary-period" key={p.start + "/" + p.end}>
                  {week.filter((_, i) => p.days.includes(i + 1)).join(", ")} ·{" "}
                  {time(p.start)} - {p.end === 1440 ? "24:00" : time(p.end)}
                  {p.end < p.start ? " (+1 день)" : ""}
                </span>
              ))}
            </dd>
          </div>
        )}
        <div>
          <dt>Звук в архиве</dt>
          <dd>
            {!policy
              ? "По команде оператора"
              : rule.audio
                ? "Включён"
                : "Выключен"}
          </dd>
        </div>
        <div>
          <dt>Срок хранения</dt>
          <dd>{days === null ? "Без ограничения по сроку" : days + " дн."}</dd>
        </div>
        <div>
          <dt>Глубина архива</dt>
          <dd>
            {depth
              ? archiveDepth(depth.oldest_us, depth.newest_us)
              : "Записей нет"}
          </dd>
        </div>
        {depth && (
          <div>
            <dt>Последний кадр</dt>
            <dd>{new Date(depth.newest_us / 1000).toLocaleString("ru")}</dd>
          </div>
        )}
      </dl>
      <Button className="rp-edit-camera" onClick={edit}>
        <ControlGlyph icon="settings-2" />
        {policy?.scope === "point"
          ? "Изменить настройки камеры"
          : "Настроить отдельно"}
      </Button>
    </section>
  );
}

function GroupMembers({
  group,
  points,
  policies,
  openCamera,
}: {
  group: CaptureGroup;
  points: Point[];
  policies: CapturePolicies;
  openCamera: (id: string) => void;
}) {
  const [query, setQuery] = useState("");
  const policy = policies.policies.find(
    (p) => p.scope === "group" && p.target === group.id,
  );
  const applies = (id: string) =>
    !!policy && policies.effective[id]?.id === policy.id;
  const overrides = new Set(
    policies.policies.filter((p) => p.scope === "point").map((p) => p.target),
  );
  const ids = new Set(group.points);
  const members = points.filter((p) => ids.has(p.id));
  const filtered = members.filter((p) => matchesQuery(query, p.name));
  return (
    <details className="rp-members">
      <summary data-policy-focus>
        <strong>Состав группы</strong>
        <span>
          {policy
            ? "Политика действует для " +
              group.points.filter(applies).length +
              " из " +
              group.points.length
            : "Камер в группе: " + group.points.length}
        </span>
      </summary>
      {needsListSearch(members.length, query) && (
        <FilterPanel
          label="Состав группы"
          queryLabel="Найти камеру в группе"
          query={query}
          onQuery={setQuery}
          onReset={() => setQuery("")}
        />
      )}
      <ul>
        {filtered.map((p) => (
          <li key={p.id}>
            <button onClick={() => openCamera(p.id)}>
              <ControlGlyph icon="cctv" />
              <span>
                <strong>{p.name}</strong>
                <small>
                  {applies(p.id)
                    ? "По политике группы"
                    : !policies.effective[p.id]
                      ? "Настройки площадки"
                      : overrides.has(p.id)
                        ? "Индивидуальная политика"
                        : "По политике другой группы"}
                </small>
              </span>
              <ControlGlyph icon="chevron-right" />
            </button>
          </li>
        ))}
      </ul>
      {!filtered.length && (
        <p>
          {query
            ? "Совпадений нет"
            : group.points.length
              ? "Камеры недоступны"
              : "В группе пока нет камер"}
        </p>
      )}
    </details>
  );
}
function Editor({
  policy,
  inherited,
  scope,
  target,
  site,
  services,
  done,
  name,
  cancel,
}: {
  policy: CapturePolicy | undefined;
  inherited: CaptureRule | undefined;
  scope: "point" | "group";
  target: string;
  site: string;
  services: Services;
  done: () => void;
  name: string;
  cancel?: () => void;
}) {
  const [rule, setRule] = useState<CaptureRule>(
      policy?.rule ?? inherited ?? defaults,
    ),
    [periods, setPeriods] = useState(() => groupPeriods(rule.schedule)),
    [priority, setPriority] = useState(policy?.priority ?? 50),
    [busy, setBusy] = useState(false),
    [error, setError] = useState(""),
    [confirm, setConfirm] = useState(false);
  function update<K extends keyof CaptureRule>(k: K, v: CaptureRule[K]) {
    setRule((r) => ({ ...r, [k]: v }));
  }
  async function save(remove = false) {
    setBusy(true);
    setError("");
    try {
      if (remove && policy)
        await services.deleteCapture(site, policy.id, policy.revision);
      else
        await services.saveCapture(site, policy?.id ?? crypto.randomUUID(), {
          scope,
          target,
          priority,
          rule: {
            ...rule,
            schedule: periods.flatMap(({ days, start, end }) =>
              days.map((day) => ({ day, start, end })),
            ),
          },
          revision: policy?.revision ?? "0",
        });
      done();
    } catch {
      setError(
        "Изменение не сохранено. Проверьте часовой пояс и редакцию политики.",
      );
    } finally {
      setBusy(false);
    }
  }
  return (
    <form
      className="rp-editor"
      onSubmit={(e) => {
        e.preventDefault();
        void save();
      }}
    >
      <strong tabIndex={-1} data-policy-focus>
        {scope === "group" ? "Политика группы" : "Индивидуальная политика"} «
        {name}»
      </strong>
      {scope === "group" && (
        <label className="rp-priority">
          Приоритет группы
          <input
            required
            type="number"
            min={0}
            max={100}
            aria-description="Если камера входит в несколько групп, действует политика с большим приоритетом."
            value={priority}
            onChange={(e) => setPriority(+e.target.value)}
          />
        </label>
      )}
      <fieldset disabled={busy}>
        <legend>Записывать</legend>
        <div className="rp-modes">
          {(
            [
              ["continuous", "Непрерывно"],
              ["manual", "Вручную"],
              ["event", "По движению"],
            ] as const
          ).map(([k, label]) => (
            <label key={k}>
              <input
                type="checkbox"
                checked={rule[k]}
                onChange={(e) => update(k, e.target.checked)}
              />
              {label}
            </label>
          ))}
        </div>
        {rule.event && (
          <div className="rp-fields">
            {(
              [
                ["pre", "До события, с", 0, 30],
                ["post", "После события, с", 1, 300],
              ] as const
            ).map(([k, label, min, max]) => (
              <label key={k}>
                {label}
                <input
                  type="number"
                  required
                  min={min}
                  max={max}
                  value={rule[k]}
                  onChange={(e) => update(k, Number(e.target.value))}
                />
              </label>
            ))}
          </div>
        )}
      </fieldset>
      <fieldset disabled={busy}>
        <legend>Недельное расписание</legend>
        {periods.length > 0 && (
          <label>
            Часовой пояс
            <input
              required
              value={rule.zone}
              list="rp-zones"
              onChange={(e) => update("zone", e.target.value)}
            />
            <datalist id="rp-zones">
              {[
                "UTC",
                Intl.DateTimeFormat().resolvedOptions().timeZone,
                "Europe/Moscow",
                "Europe/Berlin",
                "Asia/Yekaterinburg",
              ]
                .filter((v, i, a) => a.indexOf(v) === i)
                .map((z) => (
                  <option key={z}>{z}</option>
                ))}
            </datalist>
          </label>
        )}
        {periods.map((period, index) => {
          const patch = (change: Partial<Period>) =>
            setPeriods((rows) =>
              rows.map((p, n) => (n === index ? { ...p, ...change } : p)),
            );
          return (
            <div className="rp-period" key={index}>
              <div className="rp-week" role="group" aria-label="Дни записи">
                {week.map((label, dayIndex) => {
                  const day = dayIndex + 1;
                  const active = period.days.includes(day);
                  return (
                    <button
                      key={day}
                      type="button"
                      aria-pressed={active}
                      disabled={
                        active
                          ? period.days.length === 1
                          : periods.reduce((n, p) => n + p.days.length, 0) >= 56
                      }
                      onClick={() =>
                        patch({
                          days: active
                            ? period.days.filter((d) => d !== day)
                            : [...period.days, day],
                        })
                      }
                    >
                      {label}
                    </button>
                  );
                })}
              </div>
              <div className="rp-times">
                <input
                  aria-label="Начало интервала"
                  type="time"
                  required
                  value={time(period.start)}
                  onChange={(e) => {
                    if (e.target.value)
                      patch({ start: minute(e.target.value) });
                  }}
                />
                <span aria-hidden="true">-</span>
                <input
                  aria-label="Конец интервала"
                  type="time"
                  required
                  value={time(period.end)}
                  onChange={(e) => {
                    if (e.target.value)
                      patch({ end: minute(e.target.value) || 1440 });
                  }}
                />
                <IconButton
                  type="button"
                  icon="minus"
                  label="Удалить интервал"
                  onClick={() =>
                    setPeriods((rows) => rows.filter((_, n) => n !== index))
                  }
                />
              </div>
            </div>
          );
        })}
        <IconButton
          type="button"
          icon="plus"
          label="Добавить интервал"
          disabled={periods.reduce((n, p) => n + p.days.length, 0) >= 56}
          onClick={() =>
            setPeriods((rows) => [
              ...rows,
              { days: [1], start: 540, end: 1080 },
            ])
          }
        />
      </fieldset>
      <fieldset disabled={busy}>
        <legend>Архив</legend>
        <label className="rp-audio">
          <input
            type="checkbox"
            checked={rule.audio}
            onChange={(e) => update("audio", e.target.checked)}
          />
          Записывать звук
        </label>
        <div className="rp-fields">
          <label>
            Срок хранения, дни
            <input
              type="number"
              min={1}
              max={36500}
              placeholder="По настройке тома"
              value={rule.days ?? ""}
              onChange={(e) =>
                update("days", e.target.value ? +e.target.value : null)
              }
            />
          </label>
        </div>
      </fieldset>
      <footer>
        {cancel && (
          <Button type="button" disabled={busy} onClick={cancel}>
            Отмена
          </Button>
        )}
        {policy && (
          <Button
            type="button"
            disabled={busy}
            onClick={() => (confirm ? void save(true) : setConfirm(true))}
          >
            {confirm
              ? "Подтвердить сброс"
              : scope === "point"
                ? "Вернуть наследование"
                : "Снять политику группы"}
          </Button>
        )}
        <Button type="submit" variant="primary" disabled={busy}>
          {scope === "group" ? "Сохранить для группы" : "Сохранить для камеры"}
        </Button>
      </footer>
      {error && <p role="alert">{error}</p>}
    </form>
  );
}
function Volume({
  value,
  save,
}: {
  value: CaptureStorage;
  save: (days: number, reserve: number) => Promise<void>;
}) {
  const [days, term] = useState(value.days?.toString() ?? ""),
    [reserve, floor] = useState(value.reserve_percent),
    [error, fail] = useState(false),
    [busy, working] = useState(false);
  async function submit() {
    working(true);
    try {
      await save(Number(days), reserve);
      fail(false);
    } catch {
      fail(true);
    } finally {
      working(false);
    }
  }
  const v = value.volume;
  return (
    <form
      className="rp-editor"
      onSubmit={(e) => {
        e.preventDefault();
        void submit();
      }}
    >
      <div className="rp-origin">
        <strong>
          {!v
            ? "Рекордер недоступен"
            : v.state === "recording_stopped"
              ? "Запись остановлена: достигнут резерв"
              : v.state === "low_space"
                ? "Мало свободного места"
                : "Том доступен"}
        </strong>
        {v && (
          <span>
            Свободно {(v.available_bytes / 2 ** 30).toFixed(1)} из{" "}
            {(v.capacity_bytes / 2 ** 30).toFixed(1)} ГиБ
          </span>
        )}
      </div>
      {v && (
        <meter
          aria-label="Занято на томе"
          min={0}
          max={v.capacity_bytes}
          value={v.capacity_bytes - v.available_bytes}
        />
      )}
      <div className="rp-fields">
        <label>
          Срок хранения площадки, дни
          <input
            type="number"
            required
            min={1}
            max={36500}
            value={days}
            placeholder="Без ограничения"
            onChange={(e) => term(e.target.value)}
          />
        </label>
        <label>
          Резерв свободного места, %
          <input
            type="number"
            required
            min={1}
            max={90}
            value={reserve}
            onChange={(e) => floor(+e.target.value)}
          />
        </label>
      </div>
      <footer>
        <Button type="submit" variant="primary" disabled={busy}>
          Сохранить
        </Button>
      </footer>
      {error && <p role="alert">Не удалось сохранить настройки тома.</p>}
    </form>
  );
}
export default function RecordingPolicies({
  site,
  points,
  services,
}: {
  site: string;
  points: Point[];
  services: Services;
}) {
  const [view, setView] = useState("point"),
    [query, search] = useState(""),
    [chosen, choose] = useState(""),
    [editing, edit] = useState(""),
    [version, refresh] = useState(0);
  const [result, load] = useState<{
      policies: CapturePolicies;
      storage: CaptureStorage;
      groups: CaptureGroup[];
    } | null>(null),
    [error, fail] = useState(false),
    [busy, working] = useState(true);
  useEffect(() => {
    const ctl = new AbortController();
    working(true);
    Promise.all([
      services.capturePolicies(site, ctl.signal),
      services.captureStorage(site, ctl.signal),
      services.captureGroups(site, ctl.signal),
    ])
      .then(([policies, storage, groups]) => {
        if (!ctl.signal.aborted) {
          load({ policies, storage, groups });
          fail(false);
        }
      })
      .catch(() => {
        if (!ctl.signal.aborted) fail(true);
      })
      .finally(() => {
        if (!ctl.signal.aborted) working(false);
      });
    return () => ctl.abort();
  }, [site, services, version]);
  const items = (view === "point" ? points : (result?.groups ?? [])).filter(
    (p) => matchesQuery(query, p.name),
  );
  const item = items.find((p) => p.id === chosen) ?? items[0];
  const policies = result?.policies.policies ?? [],
    direct = policies.find((p) => p.target === item?.id && p.scope === view);
  const applied =
    item && view === "point" ? result?.policies.effective[item.id] : undefined;
  const group = policies.find(
    (p) => p.id === applied?.id && p.scope === "group",
  );
  const selectedGroup = result?.groups.find(
    (g) => g.id === item?.id && view === "group",
  );
  const detail = useRef<HTMLDivElement>(null);
  const focusDetail = useRef(false);
  useEffect(() => {
    if (focusDetail.current && detail.current) {
      const heading = detail.current.querySelector<HTMLElement>(
        "[data-policy-focus]",
      );
      if (heading) {
        heading.focus({ preventScroll: true });
        const bounds = heading.getBoundingClientRect();
        if (bounds.top < 0 || bounds.bottom > window.innerHeight)
          heading.scrollIntoView({ block: "nearest" });
      }
      focusDetail.current = false;
    }
  }, [view, item?.id, editing]);

  function editCamera(id: string) {
    focusDetail.current = true;
    edit(id);
  }

  function navigate(scope: string, id = "") {
    if (!scope) return;
    focusDetail.current = !!id;
    setView(scope);
    choose(id);
    search("");
    edit("");
  }
  return (
    <section
      className="ws-panel ws-pad rp-workspace"
      aria-label="Политики записи"
    >
      <FilterSegments
        label="Область настройки"
        value={view}
        options={[
          { value: "point", label: "Камеры" },
          { value: "group", label: "Группы" },
          { value: "volume", label: "Том" },
        ]}
        onChange={navigate}
      />
      {view !== "volume" && (
        <FilterPanel
          label="Фильтр политик"
          queryLabel={view === "point" ? "Найти камеру" : "Найти группу"}
          query={query}
          onQuery={(text) => {
            search(text);
            edit("");
          }}
          onReset={() => search("")}
          refresh={{
            label: "Обновить политики",
            run: () => refresh((v) => v + 1),
            busy,
          }}
        />
      )}
      {view === "volume" && (
        <IconButton
          icon="rotate-cw"
          label="Обновить состояние тома"
          onClick={() => refresh((v) => v + 1)}
        />
      )}
      {error && <p role="alert">Не удалось загрузить настройки записи.</p>}
      {!result && busy && <p role="status">Загрузка…</p>}
      {result &&
        (view === "volume" ? (
          <Volume
            key={version}
            value={result.storage}
            save={async (d, r) => {
              const storage = await services.setCaptureStorage(
                site,
                d,
                r,
                result.storage.revision,
              );
              load({ ...result, storage });
            }}
          />
        ) : (
          <div className="rp-split">
            <div
              className="rp-targets"
              aria-label={view === "point" ? "Камеры" : "Группы"}
            >
              {items.map((p) => {
                const rule =
                  view === "point"
                    ? result.policies.effective[p.id]?.rule
                    : policies.find(
                        (x) => x.scope === "group" && x.target === p.id,
                      )?.rule;
                return (
                  <button
                    type="button"
                    key={p.id}
                    aria-pressed={p.id === item?.id}
                    onClick={() => {
                      if (p.id !== item?.id) {
                        choose(p.id);
                        edit("");
                      }
                    }}
                  >
                    <strong>{p.name}</strong>
                    <small>
                      {rule
                        ? modes(rule)
                        : view === "point"
                          ? "Непрерывно"
                          : "Политика не задана"}
                    </small>
                  </button>
                );
              })}
              {!items.length && (
                <p>
                  {query
                    ? "Совпадений нет"
                    : view === "group"
                      ? "На площадке пока нет групп камер"
                      : "На площадке пока нет камер"}
                </p>
              )}
            </div>
            {item && (
              <div
                className="rp-detail"
                key={view + "/" + item.id}
                ref={detail}
                role="group"
                aria-label={item.name}
              >
                {selectedGroup && (
                  <GroupMembers
                    group={selectedGroup}
                    points={points}
                    policies={result.policies}
                    openCamera={(id) => navigate("point", id)}
                  />
                )}
                {view === "point" && editing !== item.id ? (
                  <CameraSummary
                    point={item}
                    policy={direct ?? group}
                    group={
                      group && result.groups.find((g) => g.id === group.target)
                    }
                    storage={result.storage}
                    edit={() => editCamera(item.id)}
                    openGroup={(id) => navigate("group", id)}
                  />
                ) : (
                  <Editor
                    key={[
                      direct?.id,
                      direct?.revision,
                      applied?.id,
                      applied?.revision,
                    ].join("/")}
                    scope={view === "point" ? "point" : "group"}
                    target={item.id}
                    name={item.name}
                    site={site}
                    services={services}
                    policy={direct}
                    inherited={applied?.rule}
                    done={() => {
                      if (view === "point") editCamera("");
                      refresh((v) => v + 1);
                    }}
                    {...(view === "point"
                      ? { cancel: () => editCamera("") }
                      : {})}
                  />
                )}
              </div>
            )}
          </div>
        ))}
    </section>
  );
}
