import { useEffect, useRef, useState, type FormEvent } from "react";
import { useLocation } from "react-router";
import { HttpFailure } from "../adapters/api";
import { describeError } from "../adapters/errors";
import type { Services, Session } from "../contracts/workspace";
import { useT } from "../i18n";
import { Button } from "../ui-kit/FormControls";
import { keepDesktopSignIn } from "../workflows/desktopSignIn";
import { ErrorHelpLink } from "./ErrorHelpLink";
import mark from "../../../../design/bacuri/brand/mark.svg";

export default function Login({
  services,
  onLogin,
  message,
}: {
  services: Services;
  onLogin: (user: Session) => void;
  message: string;
}) {
  const t = useT();
  const route = useLocation();
  const [helpCode, setHelpCode] = useState<string | undefined>(
    () =>
      new URLSearchParams(location.search).get("sign_in_error") ?? undefined,
  );
  // A failed corporate sign-in returns here with the cause of the error catalog.
  const [notice, setNotice] = useState(() => {
      const cause = new URLSearchParams(location.search).get("sign_in_error");
      return cause ? describeError(new HttpFailure(0, cause)) : message;
    }),
    [busy, setBusy] = useState(false),
    [enroll, setEnroll] = useState(false);
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
  async function submit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    if (busy) return;
    const fields = new FormData(e.currentTarget);
    const username = fields.get("username"),
      password = fields.get("password"),
      enrollment = fields.get("enrollment");
    if (typeof username !== "string" || typeof password !== "string") return;
    setBusy(true);
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
        setNotice(t("login.enrollmentRequired"));
        return;
      }
      // The security key prompt refused by the person counts as a rejected factor.
      if (error instanceof DOMException && error.name === "NotAllowedError")
        setNotice(t("login.keyRejected"));
      else
        setNotice(
          describeError(error, {
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
    <main className="ws-login ws-panel ws-pad">
      <img className="ws-login-mark" alt="" src={mark} />
      <h1>{t("login.title")}</h1>
      <form onSubmit={(e) => void submit(e)}>
        <label>
          {t("login.username")}
          <input required name="username" autoComplete="username" />
        </label>
        <label>
          {t("login.password")}
          <input
            required
            type="password"
            name="password"
            autoComplete="current-password"
          />
        </label>
        {enroll && (
          <label>
            {t("login.enrollment")}
            <input
              type="password"
              ref={enrollmentInput}
              name="enrollment"
              autoComplete="off"
              required
            />
          </label>
        )}
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
      <p role="status">{notice}</p>
      <ErrorHelpLink article="sign-in" code={helpCode} />
    </main>
  );
}
