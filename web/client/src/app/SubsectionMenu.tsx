import { createContext, useContext, useSyncExternalStore } from "react";
import type { Ref } from "react";
import { createPortal } from "react-dom";
import icons from "../../../../design/bacuri/icons/sprite.svg";

export const SubsectionSlot = createContext<HTMLDivElement | null>(null);
const desktop = "(min-width: 75rem)";
function observeDesktop(notify: () => void) {
  const media = window.matchMedia(desktop);
  media.addEventListener("change", notify);
  return () => media.removeEventListener("change", notify);
}
type Item = {
  label: string;
  short?: string;
  icon: string;
  name?: string;
  className?: string;
  active?: boolean;
  disabled?: boolean;
  ref?: Ref<HTMLButtonElement>;
  select: () => void;
};
export function SubsectionMenu({
  label,
  items,
  nested = false,
}: {
  label: string;
  items: Item[];
  nested?: boolean;
}) {
  const slot = useContext(SubsectionSlot);
  const wide = useSyncExternalStore(
    observeDesktop,
    () => window.matchMedia(desktop).matches,
    () => false,
  );
  const menu = (
    <div
      className="ws-submenu"
      data-nested={nested || undefined}
      role="group"
      aria-label={label}
      data-count={items.length}
    >
      {items.map(
        ({
          label,
          short,
          name = label,
          icon,
          className,
          active,
          disabled,
          ref,
          select,
        }) => (
          <button
            type="button"
            key={name}
            className={className}
            ref={ref}
            aria-label={name}
            title={name}
            aria-pressed={active}
            disabled={disabled}
            onClick={select}
          >
            <svg aria-hidden="true">
              <use href={icons + "#i-" + icon} />
            </svg>
            {short ? (
              <>
                <span className="ws-submenu-full">{label}</span>
                <span className="ws-submenu-short">{short}</span>
              </>
            ) : (
              <span>{label}</span>
            )}
          </button>
        ),
      )}
    </div>
  );
  return wide && slot ? createPortal(menu, slot) : menu;
}
