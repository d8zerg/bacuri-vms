import { useId } from "react";
import type { ComponentPropsWithRef, ReactNode } from "react";
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

type TextFieldProps = Pick<
  ComponentPropsWithRef<"input">,
  | "name"
  | "ref"
  | "value"
  | "onChange"
  | "required"
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
