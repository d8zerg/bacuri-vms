// Answers a WebAuthn challenge with the virtual authenticator of the browser. The test inserts this text before
// a call of factorAnswer(options, username) in an asynchronous WebDriver script.
function factorBytes(value) {
    return Uint8Array.from(atob(value.replaceAll("-", "+").replaceAll("_", "/")), (c) => c.charCodeAt(0));
}
function factorBase64(buffer) {
    return btoa(String.fromCharCode(...new Uint8Array(buffer)))
        .replaceAll("+", "-")
        .replaceAll("/", "_")
        .replace(/=+$/, "");
}
async function factorAnswer(options, username) {
    const common = { challenge: factorBytes(options.challenge), timeout: 90000 };
    const credential =
        options.kind === "create"
            ? await navigator.credentials.create({
                  publicKey: {
                      ...common,
                      rp: { id: options.rp_id, name: "Bacuri VMS" },
                      user: { id: new TextEncoder().encode(options.user_id), name: username, displayName: username },
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
                      allowCredentials: [{ type: "public-key", id: factorBytes(options.credential_id) }],
                  },
              });
    if (!credential || credential.type !== "public-key") throw new Error("invalid_factor");
    const response = credential.response;
    return {
        ticket: options.ticket,
        credential_id: factorBase64(credential.rawId),
        client_data: factorBase64(response.clientDataJSON),
        authenticator_data: factorBase64(
            options.kind === "create" ? response.getAuthenticatorData() : response.authenticatorData,
        ),
        ...(options.kind === "get" ? { signature: factorBase64(response.signature) } : {}),
    };
}
