import { selectLanguage, useLanguage, useT } from "../i18n";
import { Button } from "../ui-kit/FormControls";
import {
  HelpIcon,
  useHelpHref,
  type HelpTopic,
  type PublicHelpCode,
} from "./HelpLink";
import mark from "../../../../design/bacuri/brand/mark.svg";

export function SignInHeader({
  article = "sign-in",
  code,
}: {
  article?: HelpTopic;
  code?: PublicHelpCode | undefined;
}) {
  const next = useLanguage() === "en" ? "ru" : "en";
  const t = useT();
  const label = t("login.switchLanguage");
  const help = t("nav.help");
  const href = useHelpHref(article, code);
  return (
    <header className="ws-sign-in-header">
      <span className="ws-brand">
        <img alt="" src={mark} />
        Bacuri VMS
      </span>
      <div className="ws-sign-in-actions">
        <Button
          className="bk-icon-button"
          aria-label={help}
          title={help}
          onClick={() => window.open(href, "_blank", "noopener,noreferrer")}
        >
          <HelpIcon />
        </Button>
        <Button
          aria-label={label}
          title={label}
          onClick={() => void selectLanguage(next)}
        >
          {next.toUpperCase()}
        </Button>
      </div>
    </header>
  );
}
