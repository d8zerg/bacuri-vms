import assert from "node:assert/strict";
import { randomBytes, randomUUID } from "node:crypto";
import { chromium } from "playwright/test";
import { build } from "vite";

const origin = "https://workspace.test";
const site = randomUUID();
const user = randomUUID();
const resource = randomUUID();

// Register and use the same virtual authenticator through the production login adapter.
async function login(page) {
  const client = await page.context().newCDPSession(page);
  await client.send("WebAuthn.enable");
  await client.send("WebAuthn.addVirtualAuthenticator", {
    options: {
      protocol: "ctap2",
      transport: "usb",
      hasResidentKey: true,
      hasUserVerification: true,
      isUserVerified: true,
      automaticPresenceSimulation: true,
    },
  });
  const session = {
    user_id: user,
    site_roles: [],
    csrf_token: randomUUID(),
    expires_at: new Date(Date.now() + 3600000).toISOString(),
  };
  const challenges = [];
  const answers = [];
  const credentials = [];
  await page.route(origin + "/api/v1/session", (route) => {
    credentials.push(route.request().postDataJSON());
    const challenge = {
      kind: answers.length ? "get" : "create",
      rp_id: "workspace.test",
      user_id: user,
      ticket: randomBytes(32).toString("hex"),
      challenge: randomBytes(32).toString("base64url"),
    };
    if (answers.length) challenge.credential_id = answers[0].credential_id;
    challenges.push(challenge);
    return route.fulfill({ json: challenge });
  });
  await page.route(origin + "/api/v1/session/webauthn", (route) => {
    answers.push(route.request().postDataJSON());
    return route.fulfill(
      answers.length === 1 ? { status: 204 } : { json: session },
    );
  });
  const enrollment = randomBytes(32).toString("hex");
  const identity = await page.evaluate(
    (code) => window.services.login("installer", "test-password", code),
    enrollment,
  );
  assert.deepEqual(identity, session);
  assert.equal(credentials.length, 2);
  assert.ok(
    credentials.every(
      (request) =>
        request.username === "installer" &&
        request.enrollment_code === enrollment,
    ),
  );
  assert.equal(answers.length, 2);
  assert.equal(answers[0].credential_id, answers[1].credential_id);
  for (const [index, answer] of answers.entries()) {
    const data = JSON.parse(
      Buffer.from(answer.client_data, "base64url").toString(),
    );
    assert.equal(data.origin, origin);
    assert.equal(data.type, index === 0 ? "webauthn.create" : "webauthn.get");
    assert.equal(data.challenge, challenges[index].challenge);
    assert.equal(answer.ticket, challenges[index].ticket);
    assert.ok(Buffer.from(answer.authenticator_data, "base64url").length >= 37);
  }
  assert.ok(Buffer.from(answers[1].signature, "base64url").length > 0);
  return session.csrf_token;
}

// A mutation must send the current session token, and preserve a server conflict for the caller.
async function revisions(page, token) {
  const base = "/api/v1/sites/" + site;
  const requests = [];
  const layout = {
    name: "Desk",
    shared: false,
    points: [randomUUID()],
    revision: "3",
  };
  const actions = [
    [
      "deleteCapture",
      "DELETE",
      "/recording-policies/" + resource,
      [resource, "3"],
    ],
    ["deleteLayout", "DELETE", "/monitor-layouts/" + resource, [resource, "3"]],
    [
      "setCaptureStorage",
      "PUT",
      "/storage/volumes",
      [2, 10, "3"],
      { days: 2, reserve_percent: 10, revision: "3" },
    ],
    [
      "saveLayout",
      "PUT",
      "/monitor-layouts/" + resource,
      [resource, layout],
      layout,
    ],
    [
      "revokeDelegation",
      "DELETE",
      "/alert-delegations/" + resource,
      [resource],
    ],
  ];
  await page.route(origin + base + "/**", (route) => {
    const req = route.request();
    requests.push({
      path: new URL(req.url()).pathname,
      method: req.method(),
      headers: req.headers(),
      body: req.postDataJSON(),
    });
    return route.fulfill({ status: 409, json: { code: "conflict" } });
  });
  for (const [method, verb, path, args, body] of actions) {
    const result = await page.evaluate(
      async ({ method, site, args }) => {
        try {
          await window.services[method](site, ...args);
        } catch (err) {
          return { status: err.status, code: err.code };
        }
      },
      { method, site, args },
    );
    assert.deepEqual(result, { status: 409, code: "conflict" });
    const request = requests.shift();
    assert.equal(request.path, base + path);
    assert.equal(request.method, verb);
    assert.equal(request.headers["x-csrf-token"], token);
    assert.deepEqual(request.body, body ?? null);
    if (method === "deleteCapture" || method === "deleteLayout")
      assert.equal(request.headers["if-match"], '"3"');
  }
}

// Rebinding a camera must fail on a stale version and echo the server ETag on an accepted write.
async function cameraRevision(page, token) {
  const url = origin + "/api/v1/sites/" + site + "/cameras/" + resource;
  const writes = [];
  const draft = {
    name: "Updated",
    main_url: "rtsp://camera/main",
    secondary_url: "rtsp://camera/sub",
  };
  const camera = {
    ...draft,
    id: resource,
    point_id: randomUUID(),
    has_credentials: false,
    desired_revision: "7",
    applied_revision: "7",
    state: "applied",
  };
  const updated = { ...camera, desired_revision: "8", state: "applying" };
  await page.route(url, (route) => {
    const req = route.request();
    if (req.method() === "GET")
      return route.fulfill({
        headers: { ETag: '"binding-7"' },
        json: camera,
      });
    writes.push({
      method: req.method(),
      body: req.postDataJSON(),
      headers: req.headers(),
    });
    return route.fulfill({ json: updated });
  });
  async function refresh(revision) {
    return page.evaluate(
      async ({ site, id, draft, revision }) => {
        try {
          return await window.services.refreshOnvif(
            site,
            { id, desired_revision: revision },
            draft,
            new AbortController().signal,
          );
        } catch (err) {
          return { status: err.status, code: err.code };
        }
      },
      { site, id: resource, draft, revision },
    );
  }
  assert.deepEqual(await refresh("6"), {
    status: 409,
    code: "camera_binding_conflict",
  });
  assert.equal(writes.length, 0);
  assert.deepEqual(await refresh("7"), updated);
  assert.equal(writes.length, 1);
  assert.equal(writes[0].method, "PUT");
  assert.deepEqual(writes[0].body, draft);
  assert.equal(writes[0].headers["if-match"], '"binding-7"');
  assert.equal(writes[0].headers["x-csrf-token"], token);
}

async function edgeArchive(page, token) {
  const url =
    origin + "/api/v1/sites/" + site + "/cameras/" + resource + "/edge-archive";
  const recording = randomUUID();
  const interval = {
    start: "2020-01-01T12:00:00Z",
    end: "2020-01-01T12:00:01Z",
  };
  const archive = {
    supported: true,
    binding_revision: "7",
    gaps: [interval],
    recordings: [
      { id: recording, tracks: [{ kind: "Video", bounds: interval }] },
    ],
  };
  const requests = [];
  await page.route(url, (route) => {
    const req = route.request();
    requests.push({
      method: req.method(),
      body: req.postDataJSON(),
      headers: req.headers(),
    });
    return route.fulfill(
      req.method() === "GET"
        ? { json: archive }
        : { status: 409, json: { code: "camera_binding_conflict" } },
    );
  });
  const result = await page.evaluate(
    async ({ site, id, interval }) => {
      const signal = new AbortController().signal;
      const inventory = await window.services.edgeArchive(site, id, signal);
      try {
        await window.services.recoverGap(
          site,
          id,
          inventory.recordings[0].id,
          inventory.binding_revision,
          interval,
          signal,
        );
      } catch (err) {
        return { inventory, status: err.status, code: err.code };
      }
    },
    { site, id: resource, interval },
  );
  assert.deepEqual(result, {
    inventory: archive,
    status: 409,
    code: "camera_binding_conflict",
  });
  assert.deepEqual(
    requests.map((r) => r.method),
    ["GET", "POST"],
  );
  assert.equal(requests[1].headers["x-csrf-token"], token);
  assert.deepEqual(requests[1].body, {
    recording,
    binding_revision: "7",
    interval,
  });
}

export async function serviceAdapterCoverage() {
  const outDir = "/results/adapter-tests";
  const [bundle] = await build({
    configFile: false,
    logLevel: "error",
    define: { "process.env.NODE_ENV": '"production"' },
    build: {
      outDir,
      minify: false,
      target: "esnext",
      lib: {
        entry: "src/adapters/services.ts",
        formats: ["es"],
      },
      write: false,
      sourcemap: "hidden",
    },
  });
  const chunks = new Map(
    bundle.output
      .filter((item) => item.type === "chunk")
      .map((chunk) => ["/" + chunk.fileName, chunk]),
  );
  const entry = [...chunks.values()].find((chunk) => chunk.isEntry);
  assert.ok(entry);
  const browser = await chromium.launch();
  try {
    const page = await browser.newPage();
    await page.route(origin + "/", (route) =>
      route.fulfill({ contentType: "text/html", body: "<!doctype html>" }),
    );
    for (const [path, chunk] of chunks) {
      await page.route(origin + path, (route) =>
        route.fulfill({ contentType: "text/javascript", body: chunk.code }),
      );
    }
    await page.goto(origin);
    await page.coverage.startJSCoverage();
    await page.evaluate(
      async (url) => {
        const { createServices } = await import(url);
        window.services = createServices();
      },
      origin + "/" + entry.fileName,
    );
    const token = await login(page);
    await revisions(page, token);
    await cameraRevision(page, token);
    await edgeArchive(page, token);
    const scripts = await page.coverage.stopJSCoverage();
    assert.ok(
      scripts.some((script) => script.url === origin + "/" + entry.fileName),
    );
    return scripts
      .filter(
        (script) =>
          script.url.startsWith(origin + "/") &&
          chunks.has(new URL(script.url).pathname),
      )
      .map((script) => {
        const chunk = chunks.get(new URL(script.url).pathname);
        return {
          ...script,
          url: "file://" + outDir + "/" + chunk.fileName,
          sourceMap: JSON.parse(chunk.map.toString()),
        };
      });
  } finally {
    await browser.close();
  }
}
