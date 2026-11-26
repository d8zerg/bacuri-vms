import { useId, useLayoutEffect, useRef, useState } from "react";
import type { FormEvent } from "react";
import { Button, StatusIndicator, TextField } from "./FormControls";
import "./camera-connection-form.css";
import { t, useT, type MessageKey } from "../i18n";

export type CameraDraft = Readonly<{
  name: string;
  main_url: string;
  secondary_url: string;
  point_id?: string;
  replace_device_id?: string;
  onvif_selection?: string;
  credentials?: { username: string; password: string };
}>;
export type StreamCheck =
  | "available"
  | "certificate_error"
  | "unreachable"
  | "incompatible"
  | "unknown";
export type CameraCheckResult =
  | { status: "error"; reason: "disconnected" | "unknown"; message?: string }
  | { status: "profiles"; main: StreamCheck; secondary: StreamCheck };
export type CameraCheckState =
  CameraCheckResult | { status: "idle" | "checking" };

export interface CameraConnectionFormProps {
  value: CameraDraft;
  state: CameraCheckState;
  onChange: (draft: CameraDraft) => void;
  onCheck: () => void;
  submitLabel?: string;
}

const labels = {
  available: "camera.stream.available",
  unknown: "camera.stream.unknown",
  unreachable: "camera.stream.unreachable",
  incompatible: "camera.stream.incompatible",
  certificate_error: "camera.stream.certificateError",
} as const satisfies Record<StreamCheck, MessageKey>;

function streamError(value: string) {
  if (!value) return t("camera.url.required");
  if (new TextEncoder().encode(value).length > 2048)
    return t("camera.url.tooLong");
  if (!/^rtsps?:\/\//.test(value)) return t("camera.url.scheme");
  if (/^rtsps?:\/\/[^/?#]*@/.test(value)) return t("camera.url.credentials");
  if (
    value.includes("#") ||
    (value.includes("?") &&
      !/^(transmode=unicast&profile=[a-zA-Z0-9._-]{1,64}|profile=[a-zA-Z0-9._-]{1,64}(&transmode=unicast)?)$/.test(
        value.slice(value.indexOf("?") + 1),
      ))
  )
    return t("camera.url.query");
  if (/[\p{Cc}\s\\]/u.test(value) || /%(?![\da-f]{2})/i.test(value))
    return t("camera.url.characters");
  const url = URL.parse(value);
  if (!url?.hostname || url.port === "0") return t("camera.url.host");
}

function fieldErrors(value: CameraDraft) {
  let name: string | undefined;
  if (!value.name.trim()) name = t("camera.name.required");
  else if (value.name.includes("\0")) name = t("camera.name.invalid");
  else if (Array.from(value.name).length > 256) name = t("camera.name.tooLong");
  return {
    name,
    main_url: streamError(value.main_url),
    secondary_url: streamError(value.secondary_url),
  };
}

function ProfileIndicator({ value }: { value: StreamCheck }) {
  const t = useT();
  return (
    <StatusIndicator tone={value === "available" ? "success" : "danger"}>
      {t(labels[value])}
    </StatusIndicator>
  );
}

export function CameraConnectionForm({
  value,
  state,
  onChange,
  onCheck,
  submitLabel,
}: CameraConnectionFormProps) {
  const t = useT();
  const id = useId();
  const form = useRef<HTMLFormElement>(null);
  const [invalidSubmission, setInvalidSubmission] = useState<{
    field: string;
  } | null>(null);
  const errors = invalidSubmission && fieldErrors(value);
  const retry =
    state.status === "error" ||
    (state.status === "profiles" &&
      (state.main !== "available" || state.secondary !== "available"));

  useLayoutEffect(() => {
    if (!invalidSubmission) return;
    const input = form.current?.elements.namedItem(invalidSubmission.field);
    if (input instanceof HTMLInputElement) input.focus();
  }, [invalidSubmission]);

  function submit(event: FormEvent) {
    event.preventDefault();
    if (state.status === "checking") return;
    const invalid = Object.entries(fieldErrors(value)).find(
      ([, error]) => error,
    );
    if (invalid) {
      setInvalidSubmission({ field: invalid[0] });
      return;
    }
    setInvalidSubmission(null);
    onCheck();
  }

  return (
    <form
      ref={form}
      className="bc-camera"
      noValidate
      onSubmit={submit}
      aria-labelledby={id}
    >
      <header>
        <h2 id={id}>{t("camera.form.title")}</h2>
      </header>
      <div className="bc-fields">
        <TextField
          name="name"
          label={t("camera.form.name")}
          required
          value={value.name}
          error={errors?.name}
          onChange={(event) => onChange({ ...value, name: event.target.value })}
        />
        <TextField
          name="main_url"
          label={t("camera.form.main")}
          hint={t("camera.form.mainHint")}
          required
          inputMode="url"
          placeholder="rtsps://camera/main"
          readOnly={!!value.onvif_selection}
          value={value.main_url}
          error={errors?.main_url}
          onChange={(event) =>
            onChange({ ...value, main_url: event.target.value })
          }
        />
        <TextField
          name="secondary_url"
          label={t("camera.form.secondary")}
          hint={t("camera.form.secondaryHint")}
          required
          inputMode="url"
          placeholder="rtsps://camera/sub"
          readOnly={!!value.onvif_selection}
          value={value.secondary_url}
          error={errors?.secondary_url}
          onChange={(event) =>
            onChange({ ...value, secondary_url: event.target.value })
          }
        />
        <Button
          variant="primary"
          type="submit"
          aria-disabled={state.status === "checking"}
        >
          {retry
            ? t("camera.form.retry")
            : (submitLabel ?? t("camera.form.check"))}
        </Button>
      </div>
      <div role="status" aria-atomic="true" className="bc-result">
        {state.status === "checking" && (
          <StatusIndicator tone="neutral">
            {t("camera.form.checking")}
          </StatusIndicator>
        )}
        {state.status === "error" && (
          <StatusIndicator tone="danger">
            {state.reason === "disconnected"
              ? t("camera.form.disconnected")
              : (state.message ?? t("camera.form.failed"))}
          </StatusIndicator>
        )}
        {state.status === "profiles" && (
          <>
            <h3>{t("camera.form.result")}</h3>
            <dl>
              <div>
                <dt>{t("camera.form.main")}</dt>
                <dd>
                  <ProfileIndicator value={state.main} />
                </dd>
              </div>
              <div>
                <dt>{t("camera.form.secondary")}</dt>
                <dd>
                  <ProfileIndicator value={state.secondary} />
                </dd>
              </div>
            </dl>
          </>
        )}
      </div>
    </form>
  );
}
