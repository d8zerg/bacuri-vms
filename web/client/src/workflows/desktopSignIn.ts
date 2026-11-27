// Request of the desktop client to sign in through this browser. The desktop client opens
// /desktop-sign-in with its PKCE challenge, its loopback address and its state, the person
// confirms, and the browser sends the one-time code to the loopback address.

export type DesktopSignInRequest = {
  challenge: string;
  redirect: string;
  state: string;
};

const loopback =
  /^http:\/\/(127\.0\.0\.1|\[::1\]):([1-9][0-9]{3,4})\/[A-Za-z0-9_/-]*$/;
const storageKey = "bacuri.desktop.signIn";

export function desktopSignInRequest(
  params: URLSearchParams,
): DesktopSignInRequest | null {
  const challenge = params.get("code_challenge") ?? "";
  const redirect = params.get("redirect_uri") ?? "";
  const state = params.get("state") ?? "";
  const port = Number(loopback.exec(redirect)?.[2] ?? 0);
  if (
    params.get("code_challenge_method") !== "S256" ||
    !/^[A-Za-z0-9_-]{43}$/.test(challenge) ||
    redirect.length > 128 ||
    port < 1024 ||
    port > 65535 ||
    !/^[A-Za-z0-9_-]{16,128}$/.test(state)
  )
    return null;
  return { challenge, redirect, state };
}

export function desktopRedirect(
  request: DesktopSignInRequest,
  result: { code: string } | { error: "access_denied" },
): string {
  const url = new URL(request.redirect);
  if ("code" in result) url.searchParams.set("code", result.code);
  else url.searchParams.set("error", result.error);
  url.searchParams.set("state", request.state);
  return url.toString();
}

// A corporate sign-in leaves the page and returns to the monitor. The pending request is kept
// for this browser tab and restored after the return.
export function keepDesktopSignIn(search: string) {
  try {
    sessionStorage.setItem(storageKey, search);
  } catch {
    // Without storage the person starts the sign-in from the application again.
  }
}

export function takeDesktopSignIn(): string | null {
  try {
    const search = sessionStorage.getItem(storageKey);
    sessionStorage.removeItem(storageKey);
    return search;
  } catch {
    return null;
  }
}
