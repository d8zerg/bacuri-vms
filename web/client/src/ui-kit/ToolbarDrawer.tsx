import { useId, useRef, type ReactNode } from "react";
import "./settings-drawer.css";

export function SettingsIcon() {
  return (
    <svg
      aria-hidden="true"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.6"
      strokeLinejoin="round"
    >
      <path d="m10 2-.5 2.3-1.5.6L6 3.7 3.7 6l1.2 2-.6 1.5L2 10v4l2.3.5.6 1.5-1.2 2L6 20.3l2-1.2 1.5.6.5 2.3h4l.5-2.3 1.5-.6 2 1.2 2.3-2.3-1.2-2 .6-1.5 2.3-.5v-4l-2.3-.5-.6-1.5 1.2-2L18 3.7l-2 1.2-1.5-.6L14 2z" />
      <circle cx="12" cy="12" r="3.3" />
    </svg>
  );
}

export function ToolbarDrawer({
  title,
  closeLabel,
  icon,
  children,
}: {
  title: string;
  closeLabel: string;
  icon?: ReactNode;
  children: ReactNode;
}) {
  const drawer = useRef<HTMLDialogElement>(null);
  const titleId = useId();
  return (
    <>
      <button
        type="button"
        className="bk-toolbar-icon"
        aria-haspopup="dialog"
        title={title}
        aria-label={title}
        onClick={() => drawer.current?.showModal()}
      >
        {icon ?? <SettingsIcon />}
      </button>
      <dialog
        className="bk-settings-drawer"
        ref={drawer}
        aria-labelledby={titleId}
        onClick={(event) => {
          if (event.target !== event.currentTarget) return;
          const rect = event.currentTarget.getBoundingClientRect();
          if (
            event.clientX < rect.left ||
            event.clientX > rect.right ||
            event.clientY < rect.top ||
            event.clientY > rect.bottom
          )
            drawer.current?.close();
        }}
      >
        <div className="bk-settings-head">
          <h2 id={titleId}>{title}</h2>
          <button
            className="bk-toolbar-icon"
            type="button"
            aria-label={closeLabel}
            title={closeLabel}
            onClick={() => drawer.current?.close()}
          >
            <svg
              aria-hidden="true"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="1.75"
            >
              <path d="m6 6 12 12M18 6 6 18" />
            </svg>
          </button>
        </div>
        {children}
      </dialog>
    </>
  );
}
