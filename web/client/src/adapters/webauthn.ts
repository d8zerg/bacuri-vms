// Answers a WebAuthn challenge of the sign-in with the authenticator of the browser.
import type { components } from "../../../../build/contracts/api";
import { HttpFailure } from "./api";

type Challenge = components["schemas"]["FactorChallenge"];
type Answer = components["schemas"]["FactorAnswer"];

function bytes(value: string) {
  return Uint8Array.from(
    atob(value.replaceAll("-", "+").replaceAll("_", "/")),
    (c) => c.charCodeAt(0),
  );
}

function base64(buffer: ArrayBuffer) {
  return btoa(String.fromCharCode(...new Uint8Array(buffer)))
    .replaceAll("+", "-")
    .replaceAll("/", "_")
    .replace(/=+$/, "");
}

export async function factorAnswer(
  options: Challenge,
  username: string,
): Promise<Answer> {
  const common = { challenge: bytes(options.challenge), timeout: 90000 };
  const credential =
    options.kind === "create"
      ? await navigator.credentials.create({
          publicKey: {
            ...common,
            rp: { id: options.rp_id, name: "Bacuri VMS" },
            user: {
              id: new TextEncoder().encode(options.user_id),
              name: username,
              displayName: username,
            },
            pubKeyCredParams: [{ type: "public-key", alg: -7 }],
            attestation: "none",
            authenticatorSelection: { userVerification: "required" },
          },
        })
      : await navigator.credentials.get({
          publicKey: {
            ...common,
            rpId: options.rp_id,
            userVerification: "required",
            allowCredentials: [
              { type: "public-key", id: bytes(options.credential_id ?? "") },
            ],
          },
        });
  if (!(credential instanceof PublicKeyCredential))
    throw new HttpFailure(403, "invalid_factor");
  const response = credential.response;
  if (response instanceof AuthenticatorAttestationResponse)
    return {
      ticket: options.ticket,
      credential_id: base64(credential.rawId),
      client_data: base64(response.clientDataJSON),
      authenticator_data: base64(response.getAuthenticatorData()),
    };
  if (!(response instanceof AuthenticatorAssertionResponse))
    throw new HttpFailure(403, "invalid_factor");
  return {
    ticket: options.ticket,
    credential_id: base64(credential.rawId),
    client_data: base64(response.clientDataJSON),
    authenticator_data: base64(response.authenticatorData),
    signature: base64(response.signature),
  };
}
