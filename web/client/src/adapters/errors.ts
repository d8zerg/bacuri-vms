// One classification of failures for every screen: the cause from the error catalog, its handling and its text.
import catalog from "../../../../build/contracts/errors.json";
import { t, type MessageKey } from "../i18n";
import { HttpFailure } from "./api";

export type ErrorCode = keyof typeof catalog;
export type ErrorInfo = (typeof catalog)[ErrorCode];
export type Retry = "never" | "after_change" | "later" | "user_action";

// Every cause of the catalog has a message in each language; a missing one stops the type check here.
type MissingMessage = Exclude<`error.${ErrorCode}`, MessageKey>;
const complete: [MissingMessage] extends [never] ? true : MissingMessage = true;
void complete;

function known(code: string): code is ErrorCode {
  return Object.hasOwn(catalog, code);
}

export function catalogMessage(
  code: string,
  specific: Partial<Record<ErrorCode, MessageKey>> = {},
): string | undefined {
  return known(code) ? t(specific[code] ?? `error.${code}`) : undefined;
}

// The cause of a failure: the code of a Problem response, a lost connection or an unexpected failure.
export function errorCode(error: unknown): ErrorCode {
  if (error instanceof HttpFailure) {
    if (known(error.code)) return error.code;
    if (error.status === 401) return "unauthenticated";
    if (error.status === 403) return "forbidden";
    if (error.status === 404) return "not_found";
    if (error.status === 409) return "conflict";
    if (error.status === 429) return "rate_limited";
    if (error.status === 500) return "internal_error";
    if (error.status >= 500) return "dependency_unavailable";
    return "invalid_request";
  }
  if (error instanceof TypeError) return "connection_lost";
  if (error instanceof DOMException && error.name === "TimeoutError")
    return "connection_lost";
  return "internal_error";
}

export function errorInfo(code: ErrorCode): ErrorInfo {
  return catalog[code];
}

export function retryOf(error: unknown): Retry {
  return errorInfo(errorCode(error)).retry as Retry;
}

// The text of a failure. A screen passes its own wording for causes it explains in its context.
export function describeError(
  error: unknown,
  specific: Partial<Record<ErrorCode, MessageKey>> = {},
): string {
  const code = errorCode(error);
  return t(specific[code] ?? (`error.${code}` as MessageKey));
}
