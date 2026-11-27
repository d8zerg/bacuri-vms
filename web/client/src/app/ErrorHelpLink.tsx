import publicCodes from "../../../../build/contracts/help-codes.json";
import { HelpLink, type HelpTopic, type PublicHelpCode } from "./HelpLink";

function isPublicHelpCode(code?: string): code is PublicHelpCode {
  return !!code && publicCodes.includes(code);
}

export function ErrorHelpLink({
  code,
  article,
}: {
  code: string | undefined;
  article?: HelpTopic;
}) {
  return (
    <HelpLink
      code={isPublicHelpCode(code) ? code : undefined}
      article={article}
    />
  );
}
