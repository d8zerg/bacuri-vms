import { useState } from "react";
import { useSearchParams } from "react-router";
import type { Services, Session } from "../contracts/workspace";
import { errorMessageKey } from "../adapters/errors";
import { useT, type MessageKey } from "../i18n";
import { HttpFailure } from "../adapters/api";
import { publicHelpCode } from "./ErrorHelpLink";
import { SignInHeader } from "./SignInHeader";
import { Button } from "../ui-kit/FormControls";
import {
  desktopRedirect,
  desktopSignInRequest,
} from "../workflows/desktopSignIn";

// Confirmation of a desktop sign-in in the signed-in browser. The code goes only to the loopback
// address of the application on this computer, and only after the person confirms.
export function DesktopSignIn({
  services,
  identity,
}: {
  services: Services;
  identity: Session;
}) {
  const t = useT();
  const [params] = useSearchParams();
  const [request] = useState(() => desktopSignInRequest(params));
  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState<MessageKey | null>(null);
  const [helpCode, setHelpCode] = useState<string>();
  if (!request)
    return (
      <main className="ws-login ws-panel">
        <SignInHeader />
        <h1>{t("desktop.title")}</h1>
        <p role="alert">{t("desktop.invalid")}</p>
      </main>
    );
  async function allow() {
    if (!request || busy) return;
    setBusy(true);
    setNotice(null);
    setHelpCode(undefined);
    try {
      const issued = await services.issueDesktopCode({
        code_challenge: request.challenge,
        code_challenge_method: "S256",
        redirect_uri: request.redirect,
      });
      setNotice("desktop.sent");
      location.assign(desktopRedirect(request, { code: issued.code }));
    } catch (error) {
      setNotice(errorMessageKey(error));
      setHelpCode(error instanceof HttpFailure ? error.code : undefined);
      setBusy(false);
    }
  }
  return (
    <main className="ws-login ws-panel">
      <SignInHeader code={publicHelpCode(helpCode)} />
      <h1>{t("desktop.title")}</h1>
      <p>{t("desktop.explain")}</p>
      <p>
        {t("desktop.account")} <strong>{identity.username}</strong>
      </p>
      <p>{t("desktop.warning")}</p>
      <div className="ws-actions">
        <Button variant="primary" onClick={() => void allow()} disabled={busy}>
          {t("desktop.allow")}
        </Button>
        <Button
          onClick={() =>
            location.assign(
              desktopRedirect(request, { error: "access_denied" }),
            )
          }
          disabled={busy}
        >
          {t("desktop.cancel")}
        </Button>
      </div>
      <p role="status">{notice && t(notice)}</p>
    </main>
  );
}
