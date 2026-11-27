import publicCodes from "../../../../build/contracts/help-codes.json";
import { HelpLink, type HelpTopic, type PublicHelpCode } from "./HelpLink";

export function publicHelpCode(code?: string): PublicHelpCode | undefined {
  return code && publicCodes.includes(code)
    ? (code as PublicHelpCode)
    : undefined;
}

export function ErrorHelpLink({
  code,
  article,
}: {
  code: string | undefined;
  article?: HelpTopic;
}) {
  return <HelpLink code={publicHelpCode(code)} article={article} />;
}
