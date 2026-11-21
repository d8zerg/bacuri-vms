"""Independent ES256 authenticator for existing non-browser API scenarios."""

import base64
import hashlib
import json
import struct
import subprocess


def url64(data):
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def complete(http, core, root, origin, login, response):
    name = login["username"]
    status, _, challenge = response
    private = root / ("factor-" + hashlib.sha256(name.encode()).hexdigest() + ".pem")
    if status == 403 and challenge["code"] == "enrollment_required":
        code = subprocess.check_output([core, "enroll-admin", name], text=True).strip()
        status, _, challenge = http("POST", "session", {**login, "enrollment_code": code})
        assert status == 202 and challenge["kind"] == "create"
        subprocess.run(
            [
                "openssl",
                "genpkey",
                "-algorithm",
                "EC",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-out",
                private,
            ],
            check=True,
        )
    for _ in range(2):
        assert status == 202, challenge
        enroll = challenge["kind"] == "create"
        public = subprocess.check_output(["openssl", "pkey", "-in", private, "-pubout", "-outform", "DER"])[
            -65:
        ]
        assert public[0] == 4
        credential = hashlib.sha256(public).digest()
        client = json.dumps(
            {
                "type": "webauthn.create" if enroll else "webauthn.get",
                "challenge": challenge["challenge"],
                "origin": origin,
            }
        ).encode()
        auth = hashlib.sha256(challenge["rp_id"].encode()).digest() + bytes([69 if enroll else 5]) + bytes(4)
        if enroll:
            # COSE EC2 public key on P-256, algorithm ES256.
            cose = (
                bytes.fromhex("a5010203262001215820") + public[1:33] + bytes.fromhex("225820") + public[33:]
            )
            auth += bytes(16) + struct.pack(">H", len(credential)) + credential + cose
        body = {
            "ticket": challenge["ticket"],
            "credential_id": url64(credential),
            "authenticator_data": url64(auth),
            "client_data": url64(client),
        }
        if not enroll:
            sig = subprocess.check_output(
                ["openssl", "dgst", "-sha256", "-sign", private], input=auth + hashlib.sha256(client).digest()
            )
            body["signature"] = url64(sig)
        final = http("POST", "session/webauthn", body)
        if not enroll:
            return final
        assert final[0] == 204
        status, _, challenge = http("POST", "session", login)
    raise AssertionError("Missing assertion")
