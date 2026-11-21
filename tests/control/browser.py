"""Drive an isolated Linux browser over the WebDriver HTTP API."""

from contextlib import contextmanager
import json
import os
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen
from uuid import uuid4
import registry as control


@contextmanager
def browser_session(browser, credential, page, *, workspace_coverage=None, software_map=False, downloads=False):
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
    capabilities = {"browserName": browser, "acceptInsecureCerts": True}
    if downloads:
        capabilities["se:downloadsEnabled"] = True
    if software_map and browser == "chrome":
        capabilities["goog:chromeOptions"] = {"args": ["--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"]}
    result = call(
        "POST",
        "/session",
        {"capabilities": {"alwaysMatch": capabilities}},
    )
    path = "/session/" + result["sessionId"]
    coverage = os.environ.get("BACURI_BROWSER_COVERAGE") if browser == "chrome" else None

    def profiler(command, **params):
        return call("POST", path + "/goog/cdp/execute", {"cmd": "Profiler." + command, "params": params})

    try:
        workspace = workspace_coverage if browser == "chrome" else None
        if coverage or workspace:
            profiler("enable")
            profiler("startPreciseCoverage", callCount=True, detailed=True)
        call("POST", path + "/timeouts", {"script": 50000})
        if credential:
            call("POST", path + "/url", {"url": control.ORIGIN + "/session.js"})
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
        call("POST", path + "/url", {"url": control.ORIGIN + page})
        yield (
            lambda method, endpoint, body=None: call(method, path + endpoint, body),
            result["capabilities"]["browserVersion"],
        )
        if workspace:
            scripts = [s for s in profiler("takePreciseCoverage")["result"] if s["url"].startswith(control.ORIGIN + "/assets/") and s["url"].endswith(".js")]
            assert scripts, "No workspace bundle in Chrome coverage"
            (workspace / f"{uuid4()}.json").write_text(json.dumps(scripts))
        elif coverage:
            scripts = []
            for script in profiler("takePreciseCoverage")["result"]:
                url = script["url"]
                if url.startswith(control.ORIGIN + "/"):
                    source = Path("web") / urlsplit(url).path.lstrip("/")
                    if source.suffix == ".js" and source.is_file():
                        scripts.append({**script, "url": source.resolve().as_uri()})
            if not scripts:
                raise AssertionError(f"No product JavaScript coverage from {page}")
            (Path(coverage) / f"{uuid4()}.json").write_text(json.dumps({"result": scripts}))
    finally:
        call("DELETE", path)
