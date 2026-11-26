import "../../i18n/extended";
// The synchronized frame track of one point in the archive search.
import { useEffect, useRef, useState } from "react";
import type { SearchTrack, Services } from "../../contracts/workspace";

import { formatNumber, t as translate, useT } from "../../i18n";

import { iso } from "./shared";

export function Track({
  services,
  site,
  row,
  at,
  playing,
  name,
}: {
  services: Services;
  site: string;
  row: SearchTrack;
  at: number;
  playing: boolean;
  name: string;
}) {
  const t = useT();
  const video = useRef<HTMLVideoElement>(null),
    cursor = useRef(at);
  useEffect(() => {
    cursor.current = at;
  }, [at]);
  const [origin, setOrigin] = useState<number | null>(null),
    [notice, setNotice] = useState("");
  const current = row.coverage.find(
    (b) =>
      Date.parse(b.interval.start) <= at && at < Date.parse(b.interval.end),
  );
  const binding = current?.binding_revision;
  let start = current?.interval.start,
    end = current?.interval.end;
  if (current) {
    const i = row.coverage.indexOf(current);
    for (let n = i - 1; n >= 0; n--) {
      const previous = row.coverage[n];
      if (
        !previous ||
        previous.binding_revision !== binding ||
        previous.interval.end !== start
      )
        break;
      start = previous.interval.start;
    }
    for (let n = i + 1; n < row.coverage.length; n++) {
      const next = row.coverage[n];
      if (
        !next ||
        next.binding_revision !== binding ||
        next.interval.start !== end
      )
        break;
      end = next.interval.end;
    }
  }
  const reposition = useRef<((at: string) => Promise<number>) | null>(null);
  useEffect(() => {
    const v = video.current,
      ctl = new AbortController();
    setOrigin(null);
    reposition.current = null;
    setNotice("");
    if (v && start && end && binding)
      void services
        .attach(
          v,
          site,
          {
            mode: "archive",
            point: row.point_id,
            position: iso(cursor.current),
            interval: { start, end },
            binding_revision: binding,
          },
          ctl.signal,
          (message) => {
            if (!ctl.signal.aborted) setNotice(message);
          },
          undefined,
          undefined,
          (t, seek) => {
            if (!ctl.signal.aborted) {
              reposition.current = seek;
              setOrigin(t);
            }
          },
        )
        .catch(() => {});
    return () => ctl.abort();
  }, [start, end, binding, site, services, row.point_id]);
  useEffect(() => {
    const v = video.current;
    if (!v || origin === null) return;
    const offset = at / 1000 - origin;
    if (
      !playing &&
      reposition.current &&
      !Array.from({ length: v.buffered.length }, (_, i) => i).some(
        (i) => v.buffered.start(i) <= offset && offset < v.buffered.end(i),
      )
    ) {
      let active = true;
      void reposition
        .current(iso(at))
        .then((t) => {
          if (active) setOrigin(t);
        })
        .catch(() => {
          if (active) setNotice(translate("inv.frameUnavailable"));
        });
      return () => {
        active = false;
      };
    }
    if (
      offset >= 0 &&
      Math.abs(offset - v.currentTime) > (playing ? 0.16 : 0.001)
    )
      v.currentTime = offset;
    if (playing && v.paused)
      void v.play().catch(() => setNotice(translate("inv.playFailed")));
    else if (!playing && !v.paused) v.pause();
  }, [at, playing, origin]);
  return (
    <figure className="inv-track">
      <figcaption>{name}</figcaption>
      <video
        ref={video}
        muted
        playsInline
        hidden={!current || !!notice}
        aria-label={name}
      />
      {(!current || notice) && (
        <p role="status">{notice || t("timeline.gap")}</p>
      )}
      <small>
        {current
          ? (current.indexed ? t("inv.indexed") : t("inv.pendingIndex")) +
            " · " +
            (current.uncertainty_us === null
              ? t("inv.receiveTime")
              : "± " +
                t("inv.milliseconds", {
                  value: formatNumber(current.uncertainty_us / 1000),
                }))
          : ""}
      </small>
    </figure>
  );
}
