function bytes(value) {
    return Uint8Array.from(atob(value.replaceAll("-", "+").replaceAll("_", "/")), c => c.charCodeAt(0));
}
function base64(buffer) {
    return btoa(String.fromCharCode(...new Uint8Array(buffer))).replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/, "");
}
export async function factorAnswer(options, username) {
    const common = { challenge: bytes(options.challenge), timeout: 90000 };
    let credential;
    if (options.kind === "create") {
        credential = await navigator.credentials.create({ publicKey: {
            ...common, rp: { id: options.rp_id, name: "Bacuri VMS" },
            user: { id: new TextEncoder().encode(options.user_id), name: username, displayName: username },
            pubKeyCredParams: [{ type: "public-key", alg: -7 }],
            attestation: "none", authenticatorSelection: { userVerification: "required" }
        }});
    } else {
        credential = await navigator.credentials.get({ publicKey: {
            ...common, rpId: options.rp_id, userVerification: "required",
            allowCredentials: [{ type: "public-key", id: bytes(options.credential_id) }]
        }});
    }
    if (!credential || credential.type !== "public-key") throw new Error("invalid_factor");
    const response = credential.response;
    return { ticket: options.ticket, credential_id: base64(credential.rawId), client_data: base64(response.clientDataJSON),
        authenticator_data: base64(options.kind === "create" ? response.getAuthenticatorData() : response.authenticatorData),
        ...(options.kind === "get" ? {signature: base64(response.signature)} : {}) };
}
