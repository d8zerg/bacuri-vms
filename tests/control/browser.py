"""Drive an isolated Linux browser over the WebDriver HTTP API."""

from contextlib import contextmanager
import json
from urllib.error import HTTPError
from urllib.request import Request, urlopen
import registry as control


@contextmanager
def browser_session(browser, credential, page):
    root = f"http://playback-{browser}:4444"

    def call(method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        try:
            with urlopen(
                Request(
                    root + path, data=data, headers={"Content-Type": "application/json"}, method=method
                ),
                timeout=55,
            ) as res:
                return json.load(res)["value"]
        except HTTPError as error:
            raise AssertionError(error.read().decode()) from error

    control.wait_for(lambda: call("GET", "/status")["ready"], browser)
    result = call(
        "POST",
        "/session",
        {"capabilities": {"alwaysMatch": {"browserName": browser, "acceptInsecureCerts": True}}},
    )
    path = "/session/" + result["sessionId"]
    try:
        call("POST", path + "/timeouts", {"script": 50000})
        call("POST", path + "/url", {"url": control.ORIGIN + page})
        name, value = credential["Cookie"].split("=", 1)
        call(
            "POST",
            path + "/cookie",
            {
                "cookie": {
                    "name": name,
                    "value": value,
                    "path": "/",
                    "secure": True,
                    "httpOnly": True,
                    "sameSite": "Strict",
                }
            },
        )
        call("POST", path + "/refresh", {})
        yield (
            lambda method, endpoint, body=None: call(method, path + endpoint, body),
            result["capabilities"]["browserVersion"],
        )
    finally:
        call("DELETE", path)
