import { useId, useLayoutEffect, useRef, useState } from "react";
import type { FormEvent } from "react";
import { Button, StatusIndicator, TextField } from "./FormControls";
import "./camera-connection-form.css";

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
  available: "Доступен",
  unknown: "Не подтверждён",
  unreachable: "Нет связи",
  incompatible: "Несовместимый профиль",
  certificate_error: "Проверьте имя узла и доверенный сертификат камеры",
};

function streamError(value: string) {
  if (!value) return "Введите адрес потока.";
  if (new TextEncoder().encode(value).length > 2048)
    return "Адрес должен занимать не более 2048 байт.";
  if (!/^rtsps?:\/\//.test(value))
    return "Начните адрес с rtsps:// или rtsp://.";
  if (/^rtsps?:\/\/[^/?#]*@/.test(value))
    return "Удалите учётные данные из адреса.";
  if (/[?#]/.test(value)) return "Удалите параметры запроса и фрагмент адреса.";
  if (/[\p{Cc}\s\\]/u.test(value) || /%(?![\da-f]{2})/i.test(value))
    return "Проверьте символы в адресе потока.";
  const url = URL.parse(value);
  if (!url?.hostname || url.port === "0") return "Проверьте имя узла и порт.";
}

function fieldErrors(value: CameraDraft) {
  let name: string | undefined;
  if (!value.name.trim()) name = "Введите название камеры.";
  else if (value.name.includes("\0"))
    name = "Удалите недопустимый символ из названия.";
  else if (Array.from(value.name).length > 256)
    name = "Сократите название до 256 символов.";
  return {
    name,
    main_url: streamError(value.main_url),
    secondary_url: streamError(value.secondary_url),
  };
}

function ProfileIndicator({ value }: { value: StreamCheck }) {
  return (
    <StatusIndicator tone={value === "available" ? "success" : "danger"}>
      {labels[value]}
    </StatusIndicator>
  );
}

export function CameraConnectionForm({
  value,
  state,
  onChange,
  onCheck,
  submitLabel = "Проверить подключение",
}: CameraConnectionFormProps) {
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
        <h2 id={id}>Параметры камеры</h2>
      </header>
      <div className="bc-fields">
        <TextField
          name="name"
          label="Название"
          required
          value={value.name}
          error={errors?.name}
          onChange={(event) => onChange({ ...value, name: event.target.value })}
        />
        <TextField
          name="main_url"
          label="Основной поток"
          hint="Для записи в архив"
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
          label="Дополнительный поток"
          hint="Для прямого эфира"
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
          {retry ? "Повторить проверку" : submitLabel}
        </Button>
      </div>
      <div role="status" aria-atomic="true" className="bc-result">
        {state.status === "checking" && (
          <StatusIndicator tone="neutral">
            Проверяем оба потока…
          </StatusIndicator>
        )}
        {state.status === "error" && (
          <StatusIndicator tone="danger">
            {state.reason === "disconnected"
              ? "Связь с площадкой потеряна. Повторите проверку."
              : (state.message ??
                "Не удалось завершить проверку. Повторите попытку.")}
          </StatusIndicator>
        )}
        {state.status === "profiles" && (
          <>
            <h3>Результат проверки</h3>
            <dl>
              <div>
                <dt>Основной поток</dt>
                <dd>
                  <ProfileIndicator value={state.main} />
                </dd>
              </div>
              <div>
                <dt>Дополнительный поток</dt>
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
