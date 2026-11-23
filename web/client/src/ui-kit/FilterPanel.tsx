import type { ReactNode } from "react";
import { useId, useRef } from "react";
import { IconButton } from "./FormControls";
import icons from "../../../../design/bacuri/icons/sprite.svg";
import "./filter-panel.css";

export function matchesQuery(
  query: string,
  ...values: (string | undefined | null)[]
) {
  const text = values.filter(Boolean).join(" ").toLocaleLowerCase("ru");
  return query
    .trim()
    .toLocaleLowerCase("ru")
    .split(/\s+/)
    .every((word) => text.includes(word));
}

export function FilterPanel({
  label,
  queryLabel,
  query,
  onQuery,
  children,
  filtered,
  onReset,
  onApply,
  maxLength,
  refresh,
  searchDisabled,
}: {
  label: string;
  queryLabel: string;
  query: string;
  onQuery: (value: string) => void;
  children?: ReactNode;
  filtered?: boolean;
  onReset: () => void;
  onApply?: () => void;
  maxLength?: number;
  refresh?: ListRefresh;
  searchDisabled?: boolean;
}) {
  const input = useRef<HTMLInputElement>(null);
  const id = useId();
  const fields = (
    <>
      <div className="bk-filter-query">
        <label htmlFor={id}>{queryLabel}</label>
        <div className="bk-query-controls">
          <div className="bk-search-input">
            {!onApply && (
              <svg aria-hidden="true">
                <use href={icons + "#i-search"} />
              </svg>
            )}
            <input
              id={id}
              disabled={searchDisabled}
              ref={input}
              type="search"
              name="query"
              value={query}
              maxLength={maxLength}
              onKeyDown={(e) => {
                if (!onApply && e.key === "Enter") e.preventDefault();
              }}
              onChange={(e) => onQuery(e.target.value)}
            />
            {onApply && (
              <IconButton
                icon="search"
                type="submit"
                label="Найти"
                variant="primary"
              />
            )}
          </div>
          <IconButton
            icon="circle-x"
            label="Сбросить фильтры"
            disabled={!query && !filtered}
            onClick={() => {
              onReset();
              input.current?.focus();
            }}
          />
          {refresh && (
            <IconButton
              icon="rotate-cw"
              label={refresh.label}
              aria-disabled={refresh.disabled || refresh.busy}
              aria-busy={refresh.busy}
              onClick={refresh.run}
            />
          )}
        </div>
      </div>
      {children}
    </>
  );
  return onApply ? (
    <form
      role="search"
      aria-label={label}
      className="bk-filter-panel"
      onSubmit={(e) => {
        e.preventDefault();
        onApply();
      }}
    >
      {fields}
    </form>
  ) : (
    <div role="search" aria-label={label} className="bk-filter-panel">
      {fields}
    </div>
  );
}

export function needsListSearch(count: number, query: string) {
  return count > 5 || query.length > 0;
}

export type ListRefresh = {
  label: string;
  run: () => void;
  disabled?: boolean;
  busy?: boolean;
};

export function FilterSegments({
  label,
  options,
  value,
  onChange,
}: {
  label: string;
  options: readonly { value: string; label: string }[];
  value: string;
  onChange: (value: string) => void;
}) {
  return (
    <div className="bk-filter-segments" role="group" aria-label={label}>
      {options.map((option) => (
        <button
          type="button"
          key={option.value}
          aria-pressed={value === option.value}
          onClick={() => onChange(value === option.value ? "" : option.value)}
        >
          {option.label}
        </button>
      ))}
    </div>
  );
}
