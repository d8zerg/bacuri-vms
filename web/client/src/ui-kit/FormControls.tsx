import { useId } from "react";
import type { ComponentPropsWithRef, ReactNode } from "react";
import icons from "../../../../design/bacuri/icons/sprite.svg";
import "./form-controls.css";

type ButtonProps = ComponentPropsWithRef<"button"> & {
  variant?: "primary" | "secondary";
};
export function Button({
  variant = "secondary",
  type = "button",
  onClick,
  ...props
}: ButtonProps) {
  return (
    <button
      {...props}
      type={type}
      className={["bk-button", props.className].filter(Boolean).join(" ")}
      onClick={(event) => {
        if (
          props["aria-disabled"] === true ||
          props["aria-disabled"] === "true"
        ) {
          event.preventDefault();
          return;
        }
        onClick?.(event);
      }}
      data-variant={variant}
    />
  );
}

export type ControlIcon =
  | "monitor-play"
  | "monitor"
  | "layers"
  | "expand"
  | "collapse"
  | "chevron-up"
  | "chevron-down"
  | "chevron-left"
  | "chevron-right"
  | "ptz-stop"
  | "minus"
  | "plus"
  | "lens-focus"
  | "focus-near"
  | "focus-far"
  | "bookmark"
  | "tour"
  | "wiper"
  | "relay"
  | "input"
  | "power-on"
  | "power-off"
  | "headphones"
  | "mic"
  | "record-video"
  | "record-sound"
  | "history"
  | "search"
  | "circle-x"
  | "rotate-cw"
  | "file-down"
  | "move"
  | "settings-2"
  | "sun"
  | "pause"
  | "play"
  | "clock-alert"
  | "circle-check"
  | "triangle-alert"
  | "users-round"
  | "user-round"
  | "cctv"
  | "scan-line";
export function ControlGlyph({ icon }: { icon: ControlIcon }) {
  return (
    <svg aria-hidden="true" focusable="false">
      <use href={icons + "#i-" + icon} />
    </svg>
  );
}

export function IconButton({
  icon,
  label,
  ...props
}: Omit<ButtonProps, "children"> & {
  icon: ControlIcon;
  label: string;
}) {
  return (
    <Button
      {...props}
      className={["bk-icon-button", props.className].filter(Boolean).join(" ")}
      title={label}
      aria-label={label}
    >
      <ControlGlyph icon={icon} />
    </Button>
  );
}

type TextFieldProps = Pick<
  ComponentPropsWithRef<"input">,
  | "name"
  | "ref"
  | "value"
  | "onChange"
  | "required"
  | "readOnly"
  | "inputMode"
  | "placeholder"
> & {
  label: string;
  hint?: string;
  error?: string | undefined;
};

export function TextField({ label, hint, error, ...input }: TextFieldProps) {
  const id = useId();
  const descriptions = [hint && id + "-hint", error && id + "-error"]
    .filter(Boolean)
    .join(" ");
  return (
    <div className="bk-field">
      <label htmlFor={id}>{label}</label>
      <input
        {...input}
        type="text"
        id={id}
        autoCapitalize={input.inputMode === "url" ? "off" : undefined}
        autoComplete="off"
        spellCheck={input.inputMode === "url" ? false : undefined}
        aria-invalid={error ? true : undefined}
        aria-describedby={descriptions || undefined}
      />
      {hint && (
        <span id={id + "-hint"} className="bk-hint">
          {hint}
        </span>
      )}
      {error && (
        <span id={id + "-error"} className="bk-error">
          {error}
        </span>
      )}
    </div>
  );
}

export function StatusIndicator({
  tone,
  children,
}: {
  tone: "neutral" | "success" | "danger";
  children: ReactNode;
}) {
  return (
    <span className="bk-indicator" data-tone={tone}>
      {children}
    </span>
  );
}
