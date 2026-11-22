"""Build a disposable Keycloak realm and TLS identity inside the qualification network."""

import json
import os
import secrets
from pathlib import Path
import subprocess
from uuid import UUID

stand = Path("/stand/keycloak")
stand.mkdir(exist_ok=True)
subprocess.run(
    [
        "openssl",
        "req",
        "-x509",
        "-newkey",
        "rsa:2048",
        "-nodes",
        "-days",
        "1",
        "-subj",
        "/CN=keycloak",
        "-addext",
        "subjectAltName=DNS:keycloak",
        "-keyout",
        stand / "tls.key",
        "-out",
        stand / "tls.crt",
    ],
    check=True,
    stderr=subprocess.DEVNULL,
)
password = os.environ["BACURI_TEST_IDP_PASSWORD"]
realm = {
    "realm": "bacuri",
    "enabled": True,
    "sslRequired": "all",
    "duplicateEmailsAllowed": True,
    "loginWithEmailAllowed": False,
    "users": [
        {
            "id": str(UUID(int=i)),
            "username": name,
            "enabled": True,
            "emailVerified": True,
            "email": "shared@example.test",
            "firstName": "Test",
            "lastName": name,
            "requiredActions": [],
            "credentials": [{"type": "password", "value": password, "temporary": False}],
        }
        for i, name in enumerate(["operator", "unbound", "administrator", "mfa-admin"], 1)
    ],
    "clients": [
        {
            "clientId": "bacuri-site",
            "protocol": "openid-connect",
            "publicClient": False,
            "secret": password,
            "standardFlowEnabled": True,
            "directAccessGrantsEnabled": False,
            "redirectUris": ["https://workspace.test:8443/api/v1/session/oidc/callback"],
            "attributes": {"pkce.code.challenge.method": "S256"},
        }
    ],
}
otp_secret = secrets.token_hex(20)
Path("/stand/test-otp").write_text(otp_secret)
realm["users"][-1]["credentials"].append(
    {
        "type": "otp",
        "userLabel": "Qualification OTP",
        "secretData": json.dumps({"value": otp_secret}),
        "credentialData": json.dumps({"subType": "totp", "digits": 6, "period": 30, "algorithm": "HmacSHA1"}),
    }
)
realm["browserFlow"] = "Bacuri browser"
realm["authenticatorConfig"] = [
    {"alias": "MFA level", "config": {"loa-condition-level": "2", "loa-max-age": "3600"}}
]
realm["authenticationFlows"] = [
    {
        "alias": "Bacuri browser",
        "providerId": "basic-flow",
        "topLevel": True,
        "builtIn": False,
        "authenticationExecutions": [
            {"authenticator": "auth-username-password-form", "requirement": "REQUIRED", "priority": 10},
            {
                "flowAlias": "Bacuri OTP",
                "authenticatorFlow": True,
                "requirement": "CONDITIONAL",
                "priority": 20,
            },
        ],
    },
    {
        "alias": "Bacuri OTP",
        "providerId": "basic-flow",
        "topLevel": False,
        "builtIn": False,
        "authenticationExecutions": [
            {"authenticator": "conditional-user-configured", "requirement": "REQUIRED", "priority": 10},
            {
                "authenticator": "conditional-level-of-authentication",
                "authenticatorConfig": "MFA level",
                "requirement": "REQUIRED",
                "priority": 20,
            },
            {"authenticator": "auth-otp-form", "requirement": "REQUIRED", "priority": 30},
        ],
    },
]
(stand / "realm.json").write_text(json.dumps(realm))
# The unprivileged provider reads fixtures from the private stand mount.
for file in stand.iterdir():
    file.chmod(0o644)
