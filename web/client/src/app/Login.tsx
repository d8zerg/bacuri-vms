import { useEffect, useRef, useState, type FormEvent } from "react";
import { useLocation } from "react-router";
import { HttpFailure } from "../adapters/api";
import { errorMessageKey } from "../adapters/errors";
import type { Services, Session } from "../contracts/workspace";
import { useT, type MessageKey } from "../i18n";
import { Button } from "../ui-kit/FormControls";
import { keepDesktopSignIn } from "../workflows/desktopSignIn";
import { publicHelpCode } from "./ErrorHelpLink";
import { SignInHeader } from "./SignInHeader";

const requiredMessages = {
  username: "login.usernameRequired",
  password: "login.passwordRequired",
  enrollment: "login.enrollmentCodeRequired",
} as const satisfies Record<string, MessageKey>;
type LoginField = keyof typeof requiredMessages;

export default function Login({
  services,
  onLogin,
  message,
}: {
  services: Services;
  onLogin: (user: Session) => void;
  message: MessageKey | null;
}) {
  const t = useT();
  const route = useLocation();
  const [helpCode, setHelpCode] = useState<string | undefined>(
    () =>
      new URLSearchParams(location.search).get("sign_in_error") ?? undefined,
  );
  // A failed corporate sign-in returns here with the cause of the error catalog.
  const [notice, setNotice] = useState<MessageKey | null>(() =>
    helpCode ? errorMessageKey(new HttpFailure(0, helpCode)) : message,
  );
  const [invalidField, setInvalidField] = useState<LoginField>();
  const [busy, setBusy] = useState(false);
  const [enroll, setEnroll] = useState(false);
  const [corporate, setCorporate] = useState(false);
  useEffect(() => {
    const controller = new AbortController();
    void fetch("/api/v1/session/oidc/methods", {
      signal: controller.signal,
      cache: "no-store",
    })
      .then(
        async (res) => res.ok && ((await res.json()) as { oidc: boolean }).oidc,
      )
      .then((enabled) => {
        if (!controller.signal.aborted) setCorporate(enabled === true);
      })
      .catch(() => {});
    return () => controller.abort();
  }, []);
  const enrollmentInput = useRef<HTMLInputElement>(null);
  useEffect(() => {
    if (enroll) enrollmentInput.current?.focus();
  }, [enroll]);
  function fieldProps(field: LoginField) {
    const invalid = invalidField === field;
    return {
      name: field,
      required: true,
      "aria-invalid": invalid || undefined,
      "aria-describedby": invalid ? "login-notice" : undefined,
      onChange: () => {
        if (invalid) {
          setInvalidField(undefined);
          setNotice(null);
        }
      },
    };
  }
  async function submit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    if (busy) return;
    const missing =
      e.currentTarget.querySelector<HTMLInputElement>("input:invalid");
    if (missing) {
      const field = missing.name as LoginField;
      setInvalidField(field);
      setNotice(requiredMessages[field]);
      setHelpCode(undefined);
      missing.focus();
      return;
    }
    const fields = new FormData(e.currentTarget);
    const username = fields.get("username"),
      password = fields.get("password"),
      enrollment = fields.get("enrollment");
    if (typeof username !== "string" || typeof password !== "string") return;
    setBusy(true);
    setNotice(null);
    setHelpCode(undefined);
    try {
      onLogin(
        await services.login(
          username,
          password,
          typeof enrollment === "string" ? enrollment : undefined,
        ),
      );
    } catch (error) {
      setHelpCode(error instanceof HttpFailure ? error.code : undefined);
      if (
        error instanceof HttpFailure &&
        error.code === "enrollment_required"
      ) {
        setEnroll(true);
        setNotice("login.enrollmentRequired");
        return;
      }
      // The security key prompt refused by the person counts as a rejected factor.
      if (error instanceof DOMException && error.name === "NotAllowedError")
        setNotice("login.keyRejected");
      else
        setNotice(
          errorMessageKey(error, {
            invalid_credentials: "login.invalid",
            rate_limited: "login.rateLimited",
            invalid_factor: "login.keyRejected",
            internal_error: "login.failed",
            invalid_request: "login.failed",
          }),
        );
    } finally {
      setBusy(false);
    }
  }
  return (
    <main className="ws-login ws-panel">
      <SignInHeader code={publicHelpCode(helpCode)} />
      <h1>{t("section.signIn")}</h1>
      <form noValidate onSubmit={(e) => void submit(e)}>
        <label>
          {t("login.username")}
          <input {...fieldProps("username")} autoComplete="username" />
        </label>
        <label>
          {t("login.password")}
          <input
            {...fieldProps("password")}
            type="password"
            autoComplete="current-password"
          />
        </label>
        {enroll && (
          <label>
            {t("login.enrollment")}
            <input
              type="password"
              ref={enrollmentInput}
              {...fieldProps("enrollment")}
              autoComplete="off"
            />
          </label>
        )}
        <p className="ws-login-notice" id="login-notice" role="status">
          {notice && t(notice)}
        </p>
        <Button type="submit" variant="primary" disabled={busy}>
          {busy ? t("login.submitting") : t("login.submit")}
        </Button>
      </form>
      {corporate && (
        <a
          className="bk-button"
          href="/api/v1/session/oidc/start"
          onClick={() => {
            if (route.pathname === "/desktop-sign-in")
              keepDesktopSignIn(route.search);
          }}
        >
          {t("login.keycloak")}
        </a>
      )}
    </main>
  );
}
