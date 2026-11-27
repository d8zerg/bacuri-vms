import type { ReactNode } from "react";
import icons from "../../../../design/bacuri/icons/sprite.svg";
import { useT } from "../i18n";
import { ToolbarDrawer } from "../ui-kit/ToolbarDrawer";
import { HelpLink, type HelpTopic } from "./HelpLink";

export function WorkspaceAccount({
  username,
  helpTopic,
  logout,
  children,
}: {
  username: string;
  helpTopic: HelpTopic;
  logout: () => void;
  children: ReactNode;
}) {
  const t = useT();
  const userIcon = (
    <svg aria-hidden="true">
      <use href={icons + "#i-user-round"} />
    </svg>
  );
  const signOut = (iconOnly: boolean) => (
    <button
      className={iconOnly ? "bk-toolbar-icon" : "bk-button"}
      title={t("session.logout")}
      type="button"
      aria-label={t("session.logout")}
      onClick={logout}
    >
      <svg aria-hidden="true">
        <use href={icons + "#i-log-out"} />
      </svg>
      {!iconOnly && t("session.logout")}
    </button>
  );
  return (
    <>
      <div className="ws-account-compact">
        <ToolbarDrawer
          title={username}
          closeLabel={t("nav.closeAccount")}
          icon={userIcon}
        >
          <div className="ws-account-actions">
            <HelpLink article={helpTopic} />
            {signOut(false)}
          </div>
        </ToolbarDrawer>
      </div>
      {children}
      <div className="ws-account-wide">
        <span className="ws-account-name" title={username}>
          {userIcon}
          <span>{username}</span>
        </span>
        {signOut(true)}
      </div>
    </>
  );
}
