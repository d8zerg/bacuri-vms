import { useEffect, useRef } from "react";
import type { VideoSearch } from "../contracts/workspace";

type Region = NonNullable<VideoSearch["region"]>;
export const localDateTime = (ms: number) =>
  new Date(ms - new Date(ms).getTimezoneOffset() * 60000)
    .toISOString()
    .slice(0, 23);

export function Preview({ pixels }: { pixels: string }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const ctx = canvas.current?.getContext("2d");
    if (!ctx) return;
    const data = atob(pixels);
    if (data.length !== 64 * 48) return;
    const frame = ctx.createImageData(64, 48);
    for (let i = 0; i < data.length; i++) {
      const n = data.charCodeAt(i);
      frame.data.set([n, n, n, 255], 4 * i);
    }
    ctx.putImageData(frame, 0, 0);
  }, [pixels]);
  return <canvas ref={canvas} width={64} height={48} aria-hidden="true" />;
}
export function imageCoordinates(e: React.PointerEvent<HTMLDivElement>) {
  const r = e.currentTarget.getBoundingClientRect();
  return {
    x: Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)),
    y: Math.max(0, Math.min(1, (e.clientY - r.top) / r.height)),
  };
}
export function RegionField({
  region,
  change,
  image,
}: {
  region: Region;
  change: (r: Region) => void;
  image?: string | undefined;
}) {
  const start = useRef<{ x: number; y: number } | null>(null);
  return (
    <details>
      <summary>Область движения</summary>
      <div
        className="inv-roi"
        onPointerDown={(e) => {
          start.current = imageCoordinates(e);
          e.currentTarget.setPointerCapture(e.pointerId);
        }}
        onPointerCancel={() => {
          start.current = null;
        }}
        onPointerUp={() => {
          start.current = null;
        }}
        onPointerMove={(e) => {
          const a = start.current;
          if (!a) return;
          const b = imageCoordinates(e);
          if (Math.abs(a.x - b.x) > 0.015 && Math.abs(a.y - b.y) > 0.02)
            change({
              left: Math.min(a.x, b.x),
              top: Math.min(a.y, b.y),
              right: Math.max(a.x, b.x),
              bottom: Math.max(a.y, b.y),
            });
        }}
      >
        {image && <Preview pixels={image} />}
        <span
          style={{
            left: region.left * 100 + "%",
            top: region.top * 100 + "%",
            width: (region.right - region.left) * 100 + "%",
            height: (region.bottom - region.top) * 100 + "%",
          }}
        />
      </div>
      <div className="inv-bounds">
        {(["left", "top", "right", "bottom"] as const).map((edge, i) => (
          <label key={edge}>
            {["Слева, %", "Сверху, %", "Справа, %", "Снизу, %"][i]}
            <input
              type="number"
              min={0}
              max={100}
              value={Math.round(region[edge] * 100)}
              onChange={(e) => {
                const next = {
                  ...region,
                  [edge]: Number(e.target.value) / 100,
                };
                if (
                  next.left < next.right &&
                  next.top < next.bottom &&
                  next[edge] >= 0 &&
                  next[edge] <= 1
                )
                  change(next);
              }}
            />
          </label>
        ))}
      </div>
    </details>
  );
}
