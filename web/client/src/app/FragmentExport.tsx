import { useEffect, useRef, useState } from "react";
import type { ExportOptions, Interval, Services } from "../contracts/workspace";
import "./fragment-export.css";
import { describeError } from "../adapters/errors";
import { Button, TextField } from "../ui-kit/FormControls";
import { useT } from "../i18n";

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
  const t = useT();
  const [start, setStart] = useState(interval.start);
  const [end, setEnd] = useState(interval.end);
  const [mode, setMode] = useState<ExportOptions["mode"]>("selection");
  const [reason, setReason] = useState(""),
    [sound, setSound] = useState(false);
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
        mode !== "selection" || sound
          ? { mode, audio: sound, reason }
          : undefined,
      );
      if (!ctl.signal.aborted) setUrl(URL.createObjectURL(file));
    } catch (cause) {
      if (!ctl.signal.aborted)
        setError(
          describeError(cause, {
            forbidden: "export.forbidden",
            invalid_request: "export.badInterval",
            media_unavailable: "export.ambiguous",
            export_too_large: "export.tooLarge",
            internal_error: "export.failed",
          }),
        );
    } finally {
      controller.current = null;
      if (!ctl.signal.aborted) setBusy(false);
    }
  }
  return (
    <details className="ws-export">
      <summary>{t("export.title")}</summary>
      <form
        onSubmit={(event) => {
          event.preventDefault();
          void prepare();
        }}
      >
        <p className="ws-export-hint">{t("export.hint")}</p>
        <fieldset>
          <TextField
            name="exportStart"
            label={t("export.start")}
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
            label={t("export.end")}
            value={end}
            required
            readOnly={busy}
            onChange={(e) => {
              setEnd(e.target.value);
              setUrl("");
              setError("");
            }}
          />
          <label>
            {t("export.representation")}
            <select
              value={mode}
              disabled={busy}
              onChange={(e) => {
                setMode(e.target.value as ExportOptions["mode"]);
                setUrl("");
              }}
            >
              <option value="selection">{t("export.selection")}</option>
              <option value="original">{t("export.original")}</option>
              <option value="derivative">{t("export.derivative")}</option>
            </select>
          </label>
          <label>
            <input
              type="checkbox"
              checked={sound}
              disabled={busy}
              onChange={(e) => {
                setSound(e.target.checked);
                setUrl("");
              }}
            />
            {t("export.audio")}
          </label>
          {(mode !== "selection" || sound) && (
            <TextField
              name="exportReason"
              label={t("export.reason")}
              required
              value={reason}
              onChange={(e) => {
                setReason(e.target.value);
                setUrl("");
              }}
            />
          )}
          <Button
            ref={submit}
            type="submit"
            aria-disabled={busy}
            variant="primary"
          >
            {busy ? t("export.preparing") : t("export.prepare")}
          </Button>
        </fieldset>
        {error && <p role="alert">{error}</p>}
        {url && (
          <a
            className="bk-button"
            ref={download}
            href={url}
            download={`bacuri-${point}-${start}.tar`}
          >
            {t("export.save")}
          </a>
        )}
      </form>
    </details>
  );
}
