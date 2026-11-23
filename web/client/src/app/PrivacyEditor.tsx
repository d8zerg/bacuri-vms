import { useRef, useState } from "react";
import type { PointerEvent } from "react";
import type { components } from "../../../../build/contracts/api";
import { Button } from "../ui-kit/FormControls";

type Policy = components["schemas"]["PrivacyPolicy"];
type Box = components["schemas"]["MaskRegions"][number];
const outputs = {
  live: "Эфир",
  archive: "Архив",
  thumbnail: "Миниатюры",
  export: "Экспорт",
} as const;
const edges = {
  left: "Слева",
  top: "Сверху",
  right: "Справа",
  bottom: "Снизу",
} as const;
type Output = keyof typeof outputs;
const outputKeys = Object.keys(outputs) as Output[];
const edgeKeys = Object.keys(edges) as (keyof Box)[];
const valid = (box: Box) =>
  edgeKeys.every(
    (k) => Number.isFinite(box[k]) && box[k] >= 0 && box[k] <= 1,
  ) &&
  box.left < box.right &&
  box.top < box.bottom;

function position(e: PointerEvent<HTMLDivElement>) {
  const rect = e.currentTarget.getBoundingClientRect();
  return {
    x: Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width)),
    y: Math.max(0, Math.min(1, (e.clientY - rect.top) / rect.height)),
  };
}

export function PrivacyEditor({
  policy,
  available,
  save,
}: {
  policy: Policy | undefined;
  available: boolean;
  save: (p: Policy) => void;
}) {
  const [selected, select] = useState({
    live: !!policy?.live,
    archive: !!policy?.archive,
    thumbnail: !!policy?.thumbnail,
    export: !!policy?.export,
  });
  const [boxes, setBoxes] = useState<Box[]>(policy?.regions ?? []);
  const [drag, setDrag] = useState<Box | null>(null);
  const start = useRef<{ x: number; y: number; id: number } | null>(null);
  const blocked = outputKeys.filter((k) => selected[k]).map((k) => outputs[k]);
  function draw(e: PointerEvent<HTMLDivElement>) {
    if (!start.current || e.pointerId !== start.current.id) return null;
    const p = position(e);
    return {
      left: Math.min(start.current.x, p.x),
      right: Math.max(start.current.x, p.x),
      top: Math.min(start.current.y, p.y),
      bottom: Math.max(start.current.y, p.y),
    };
  }
  const invalid = boxes.some((b) => !valid(b));
  return (
    <form
      className="ws-privacy"
      onSubmit={(e) => {
        e.preventDefault();
        if (available && !invalid) save({ ...selected, regions: boxes });
      }}
    >
      <fieldset>
        <legend>Закрыть выдачу изображения</legend>
        <div className="ws-privacy-outputs">
          {outputKeys.map((k) => (
            <label key={k}>
              <input
                type="checkbox"
                name={k}
                checked={selected[k]}
                onChange={(e) => select({ ...selected, [k]: e.target.checked })}
              />
              {outputs[k]}
            </label>
          ))}
        </div>
      </fieldset>
      <details>
        <summary>Области маски ({boxes.length})</summary>
        <p>
          Отметьте область на схеме кадра или задайте её границы в процентах.
          Области сохраняются для маскирования.
        </p>
        <div
          className="ws-mask-frame"
          role="img"
          aria-label="Схема областей кадра"
          onPointerDown={(e) => {
            if (
              !available ||
              !e.isPrimary ||
              e.button !== 0 ||
              boxes.length >= 32
            )
              return;
            start.current = { ...position(e), id: e.pointerId };
            e.currentTarget.setPointerCapture(e.pointerId);
          }}
          onPointerMove={(e) => {
            if (e.pointerId === start.current?.id) setDrag(draw(e));
          }}
          onPointerUp={(e) => {
            if (e.pointerId !== start.current?.id) return;
            const b = draw(e);
            if (available && b && valid(b) && boxes.length < 32)
              setBoxes([...boxes, b]);
            start.current = null;
            setDrag(null);
          }}
          onLostPointerCapture={(e) => {
            if (e.pointerId !== start.current?.id) return;
            start.current = null;
            setDrag(null);
          }}
        >
          {(drag ? [...boxes, drag] : boxes).map(
            (b, i) =>
              valid(b) && (
                <span
                  key={i}
                  style={{
                    left: b.left * 100 + "%",
                    top: b.top * 100 + "%",
                    width: (b.right - b.left) * 100 + "%",
                    height: (b.bottom - b.top) * 100 + "%",
                  }}
                >
                  {i + 1}
                </span>
              ),
          )}
        </div>
        {boxes.map((b, i) => (
          <fieldset key={i}>
            <legend>Область {i + 1}</legend>
            <div className="ws-mask-coordinates">
              {edgeKeys.map((key) => (
                <label key={key}>
                  {edges[key]}, %
                  <input
                    type="number"
                    required
                    step="any"
                    min="0"
                    max="100"
                    value={
                      Number.isFinite(b[key])
                        ? Number((b[key] * 100).toPrecision(14))
                        : ""
                    }
                    onChange={(e) =>
                      setBoxes(
                        boxes.map((box, j) =>
                          j === i
                            ? { ...box, [key]: e.target.valueAsNumber / 100 }
                            : box,
                        ),
                      )
                    }
                  />
                </label>
              ))}
              <Button onClick={() => setBoxes(boxes.filter((_, j) => i !== j))}>
                Удалить область {i + 1}
              </Button>
            </div>
          </fieldset>
        ))}
        <Button
          disabled={boxes.length >= 32}
          onClick={() =>
            setBoxes([
              ...boxes,
              { left: 0.25, top: 0.25, right: 0.75, bottom: 0.75 },
            ])
          }
        >
          Добавить область
        </Button>
      </details>
      {invalid && (
        <p role="alert">
          Проверьте границы областей: левая и верхняя границы должны быть меньше
          правой и нижней.
        </p>
      )}
      <p id="privacy-effect" className="ws-privacy-effect" role="status">
        {blocked.length ? (
          <>
            После применения весь кадр будет закрыт для выбранных видов выдачи.
            <strong>{blocked.join(" · ")}</strong>
          </>
        ) : (
          "После применения эта политика не будет закрывать изображение."
        )}
      </p>
      <Button
        variant="primary"
        type="submit"
        aria-describedby="privacy-effect"
        disabled={!available || invalid}
      >
        Применить политику
      </Button>
    </form>
  );
}
