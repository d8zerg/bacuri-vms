import { useEffect, useRef, useState } from "react";
import type { Interval, Services } from "../contracts/workspace";
import "./fragment-export.css";
import { HttpFailure } from "../adapters/api";
import { Button, TextField } from "../ui-kit/FormControls";

export default function FragmentExport({
  services,
  site,
  point,
  alert,
  interval,
}: {
  services: Services;
  site: string;
  point: string;
  alert: string | null;
  interval: Interval;
}) {
  const [start, setStart] = useState(interval.start);
  const [end, setEnd] = useState(interval.end);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [url, setUrl] = useState("");
  const submit = useRef<HTMLButtonElement>(null);
  const download = useRef<HTMLAnchorElement>(null);
  useEffect(() => {
    if (url && submit.current === document.activeElement)
      download.current?.focus();
  }, [url]);
  const controller = useRef<AbortController | null>(null);
  useEffect(() => () => controller.current?.abort(), []);
  useEffect(
    () => () => {
      if (url) URL.revokeObjectURL(url);
    },
    [url],
  );

  async function prepare() {
    if (controller.current) return;
    const ctl = new AbortController();
    controller.current = ctl;
    setBusy(true);
    setError("");
    setUrl("");
    try {
      const file = await services.exportFragment(
        site,
        point,
        { start, end },
        alert,
        ctl.signal,
      );
      if (!ctl.signal.aborted) setUrl(URL.createObjectURL(file));
    } catch (cause) {
      const status = cause instanceof HttpFailure ? cause.status : 0;
      if (!ctl.signal.aborted)
        setError(
          status === 403
            ? "Нет права на экспорт выбранного интервала."
            : status === 400
              ? "Укажите начало и конец в UTC. Максимальный интервал - один час."
              : status === 409
                ? "В интервале нет однозначно доступной записи."
                : status === 413
                  ? "Сократите интервал экспорта."
                  : "Не удалось получить комплект. Повторите запрос.",
        );
    } finally {
      controller.current = null;
      if (!ctl.signal.aborted) setBusy(false);
    }
  }
  return (
    <details className="ws-export">
      <summary>Экспорт фрагмента</summary>
      <form
        onSubmit={(event) => {
          event.preventDefault();
          void prepare();
        }}
      >
        <p className="ws-export-hint">
          Укажите интервал до одного часа. Время в UTC заканчивается буквой Z.
        </p>
        <fieldset>
          <TextField
            name="exportStart"
            label="Начало интервала (UTC)"
            value={start}
            required
            readOnly={busy}
            onChange={(e) => {
              setStart(e.target.value);
              setUrl("");
              setError("");
            }}
          />
          <TextField
            name="exportEnd"
            label="Конец интервала (UTC)"
            value={end}
            required
            readOnly={busy}
            onChange={(e) => {
              setEnd(e.target.value);
              setUrl("");
              setError("");
            }}
          />
          <Button
            ref={submit}
            type="submit"
            aria-disabled={busy}
            variant="primary"
          >
            {busy ? "Подготовка комплекта…" : "Подготовить комплект"}
          </Button>
        </fieldset>
        {error && <p role="alert">{error}</p>}
        {url && (
          <a
            ref={download}
            href={url}
            download={`bacuri-${point}-${start}.tar`}
          >
            Сохранить комплект
          </a>
        )}
      </form>
    </details>
  );
}
