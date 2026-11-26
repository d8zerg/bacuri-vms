import "../../i18n/extended";
// Retention days and the free space reserve of the archive volume.
import { useState } from "react";
import type { CaptureStorage } from "../../contracts/workspace";
import { Button } from "../../ui-kit/FormControls";

import { formatNumber, locale, useT } from "../../i18n";

export function Volume({
  value,
  save,
}: {
  value: CaptureStorage;
  save: (days: number, reserve: number) => Promise<void>;
}) {
  const t = useT();
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
            ? t("rp.state.offline")
            : v.state === "recording_stopped"
              ? t("rp.stopped")
              : v.state === "low_space"
                ? t("rp.lowSpace")
                : t("rp.volumeOk")}
        </strong>
        {v && (
          <span>
            {t("rp.free", {
              free: formatNumber(v.available_bytes / 2 ** 30, {
                minimumFractionDigits: 1,
                maximumFractionDigits: 1,
              }),
              total: formatNumber(v.capacity_bytes / 2 ** 30, {
                minimumFractionDigits: 1,
                maximumFractionDigits: 1,
              }),
            })}
          </span>
        )}
      </div>
      {v && (
        <meter
          aria-label={t("rp.used")}
          min={0}
          max={v.capacity_bytes}
          value={v.capacity_bytes - v.available_bytes}
        />
      )}
      {v?.replica && (
        <p>
          {v.replica.online === false
            ? t("rp.replicaOffline")
            : t("rp.replica")}{" "}
          · {t("rp.verified", { count: v.replica.verified })}
          {!!v.replica.pending &&
            " · " + t("rp.pending", { count: v.replica.pending })}
          {!!v.replica.failed &&
            " · " + t("rp.failed", { count: v.replica.failed })}
        </p>
      )}
      {v?.write_bytes_per_second && (
        <p>
          {t("rp.forecast")} ·{" "}
          {Math.min(
            Number(days) || Infinity,
            Math.max(0, v.capacity_bytes * (1 - reserve / 100)) /
              v.write_bytes_per_second /
              86400,
          ).toLocaleString(locale(), { maximumFractionDigits: 1 })}{" "}
          {t("rp.daysShort")}
        </p>
      )}
      <div className="rp-fields">
        <label>
          {t("rp.siteRetention")}
          <input
            type="number"
            required
            min={1}
            max={36500}
            value={days}
            placeholder={t("rp.unlimited")}
            onChange={(e) => term(e.target.value)}
          />
        </label>
        <label>
          {t("rp.reserve")}
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
          {t("action.save")}
        </Button>
      </footer>
      {error && <p role="alert">{t("rp.volumeSaveFailed")}</p>}
    </form>
  );
}
