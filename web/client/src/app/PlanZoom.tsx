import { useId } from "react";

export function PlanZoom({
  label,
  levels,
  value,
  onChange,
}: {
  label: string;
  levels: number[];
  value: number;
  onChange: (value: number) => void;
}) {
  const name = useId();
  return (
    <div role="radiogroup" aria-label={label} className="ws-plan-zoom">
      {levels.map((level) => (
        <label key={level}>
          <input
            type="radio"
            name={name}
            checked={value === level}
            onChange={() => onChange(level)}
          />
          <span>{level * 100}%</span>
        </label>
      ))}
    </div>
  );
}
