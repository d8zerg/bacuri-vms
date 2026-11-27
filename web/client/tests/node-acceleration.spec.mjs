import { test, expect, checkAccessibility } from "./browser.mjs";

const kinds = [
  "live_mask",
  "live_profile",
  "live_analysis",
  "export",
  "archive_analysis",
  "keyframe",
  "thumbnail",
];
const stages = ["inventory", "decode", "encode", "inference", "workloads"];
const nodeId = "00000000-0000-4000-8000-000000000003";
const gpu = "gpu-0";
const device = {
  id: gpu,
  model: "NVIDIA RTX A2000",
  backend: "nvidia",
  driver: "550.90",
  memory_bytes: 6000000000,
};
const costs = kinds.map((task, i) => ({
  task,
  time_ppm: [320000, 260000, 180000, 750000, 180000, 80000, 10000][i],
  encoder_sessions: ["live_mask", "live_profile", "export"].includes(task)
    ? 1
    : 0,
  memory_bytes: 400000000,
  exclusive: false,
}));
const result = {
  node_id: nodeId,
  measured_at: "2026-01-01T12:00:00Z",
  fingerprint: "a".repeat(64),
  measurements: [],
  devices: [{ id: "cpu", model: "CPU", backend: "cpu" }, device],
};
const labels = {
  en: {
    node: "Node",
    manual: "Manual",
    auto: "Automatic",
    cpu: "CPU only",
    apply: "Apply changes",
    cancel: "Cancel check",
    again: "Check again",
    mask: "Live privacy masks",
    exp: "Video export",
    reject: "Outside the device budget",
    fallback: "Fallback warning",
    budget: "Available budget",
  },
  ru: {
    node: "Узел",
    manual: "Вручную",
    auto: "Авто",
    cpu: "Только CPU",
    apply: "Применить изменения",
    cancel: "Отменить проверку",
    again: "Проверить снова",
    mask: "Маски приватности в эфире",
    exp: "Экспорт видео",
    reject: "Превышают бюджет устройства",
    fallback: "Предупреждение о переходе на CPU",
    budget: "Свободный бюджет",
  },
};

function proposal(selection, revision) {
  let time = 0,
    sessions = 0,
    memory = 0;
  const rejected_tasks = [];
  const decisions = costs.map((c) => {
    const selected =
      selection.mode === "auto" || selection.gpu_tasks?.includes(c.task);
    let reason = selection.mode === "cpu_only" ? "cpu_only" : "not_selected";
    if (selected) {
      if (time + c.time_ppm > 1000000 || sessions + c.encoder_sessions > 2) {
        reason = "budget_exceeded";
        if (selection.mode === "manual") rejected_tasks.push(c.task);
      } else {
        reason = "within_budget";
        time += c.time_ppm;
        sessions += c.encoder_sessions;
        memory += c.memory_bytes;
      }
    }
    return {
      task: c.task,
      device_id: reason === "within_budget" ? gpu : "cpu",
      reason,
    };
  });
  return {
    node_id: nodeId,
    revision,
    mode: selection.mode,
    gpu_tasks: selection.gpu_tasks ?? [],
    decisions,
    rejected_tasks,
    applicable: !rejected_tasks.length,
    issue: rejected_tasks.length ? "acceleration_budget_exceeded" : null,
    devices:
      selection.mode === "cpu_only"
        ? []
        : [
            {
              device_id: gpu,
              time_ppm_used: time,
              encoder_sessions: 2,
              encoder_sessions_used: sessions,
              memory_bytes: device.memory_bytes,
              memory_bytes_used: memory,
              costs,
            },
          ],
  };
}

async function nodeApi(page, mode = "cpu_only") {
  let saved = { mode },
    revision = 0,
    sequence = 0;
  const state = {
    stage: "completed",
    offline: false,
    releasing: false,
    conflict: false,
    starts: [],
    writes: [],
    cancels: [],
  };
  function probe() {
    const jobId =
      "00000000-0000-4000-9000-" + String(sequence).padStart(12, "0");
    return {
      node_id: nodeId,
      stale: false,
      result,
      job: {
        id: jobId,
        reason: sequence ? "prepare_gpu" : "startup",
        state: state.stage,
        steps: stages.map((name, i) => ({
          name,
          state:
            state.stage === "completed" || i < 2
              ? "completed"
              : i === 2
                ? state.stage
                : "pending",
          total: 4,
          completed: i < 2 || state.stage === "completed" ? 4 : i === 2 ? 2 : 0,
        })),
      },
      scheduler: {
        mode: saved.mode,
        devices: [],
        journal: [],
        tasks:
          saved.mode === "auto"
            ? { cpu: 1, gpu: 2, queue: 0 }
            : { cpu: 0, gpu: 0, queue: 0 },
        warnings:
          saved.mode === "auto"
            ? [{ device_id: gpu, reason: "session_limit" }]
            : [],
        assignments:
          saved.mode === "auto"
            ? [
                {
                  task: "live_mask",
                  device_id: gpu,
                  running_device_id: gpu,
                  reason: "within_budget",
                  count: 2,
                },
                {
                  task: "live_mask",
                  device_id: "cpu",
                  running_device_id: state.releasing ? gpu : "cpu",
                  reason: "session_limit",
                  ...(state.releasing ? { releasing_device_id: gpu } : {}),
                  count: 1,
                },
              ]
            : [],
      },
    };
  }
  await page.route(
    "**/api/v1/sites/*/nodes/*/acceleration**",
    async (route) => {
      const r = route.request(),
        path = new URL(r.url()).pathname.split("/acceleration")[1];
      if (r.method() !== "GET")
        expect(r.headers()["x-csrf-token"]).toMatch(/^fixture-csrf/);
      if (path === "/probe" && state.offline)
        return route.fulfill({
          status: 503,
          json: { code: "media_service_unavailable" },
        });
      if (path === "/probe" && r.method() === "POST") {
        state.starts.push(r.postDataJSON().reason);
        sequence++;
        state.stage = "running";
        return route.fulfill({ status: 202, json: probe() });
      }
      if (path.startsWith("/probe/")) {
        expect(path).toBe("/probe/" + probe().job.id);
        state.cancels.push(probe().job.id);
        state.stage = "cancelled";
        return route.fulfill({ json: probe() });
      }
      if (path === "/probe") return route.fulfill({ json: probe() });
      if (path === "/preview")
        return route.fulfill({ json: proposal(r.postDataJSON(), revision) });
      if (r.method() === "PUT") {
        expect(r.headers()["if-match"]).toBe(JSON.stringify(String(revision)));
        if (state.conflict) {
          state.conflict = false;
          revision++;
          return route.fulfill({
            status: 412,
            json: { code: "precondition_failed" },
          });
        }
        saved = r.postDataJSON();
        state.writes.push(saved);
        revision++;
      }
      return route.fulfill({ json: proposal(saved, revision) });
    },
  );
  return state;
}

for (const lang of ["en", "ru"])
  test(
    "Node acceleration settings and recovery in " + lang,
    async ({ page }, info) => {
      const l = labels[lang],
        state = await nodeApi(page);
      await page.setViewportSize({ width: 1440, height: 900 });
      await page.goto(
        "/iframe.html?viewMode=story&id=workspace--node&globals=language:" +
          lang,
      );
      await page.getByRole("link", { name: l.node, exact: true }).click();
      await expect(
        page.locator('a[href*="article=node-acceleration"]').first(),
      ).toBeVisible();
      const view = page.locator(".na-page"),
        radio = (name) => view.getByRole("radio", { name, exact: true });
      const button = (name) => view.getByRole("button", { name, exact: true });
      const stage = (expected) =>
        expect(view.locator(".na-state")).toHaveAttribute(
          "data-tone",
          expected,
        );
      async function capture(name) {
        if (info.project.name.startsWith("chromium")) {
          await page.evaluate(() => document.fonts.ready);
          await expect(page).toHaveScreenshot(
            "node-" + lang + "-" + name + ".png",
            { fullPage: true, animations: "disabled" },
          );
        }
      }
      await expect(radio(l.cpu)).toBeChecked();
      await radio(l.cpu).focus();
      await page.keyboard.press("ArrowRight");
      await expect(radio(l.manual)).toBeChecked();
      await stage("running");
      expect(state.starts).toEqual(["prepare_gpu"]);
      await expect(view.getByRole("progressbar")).toHaveAttribute("value", "2");
      await expect(button(l.apply)).toBeDisabled();
      await capture("progress");
      await page.emulateMedia({ reducedMotion: "reduce" });
      await expect(view.getByRole("progressbar")).toHaveCount(0);
      await expect(view.getByText("2 / 4")).toBeVisible();
      await page.emulateMedia({ reducedMotion: "no-preference" });
      await button(l.cancel).click();
      await stage("cancelled");
      expect(state.cancels).toHaveLength(1);
      await capture("cancelled");
      await button(l.again).click();
      await stage("running");
      state.stage = "failed";
      await stage("failed");
      await capture("failed");
      await button(l.again).click();
      await stage("running");
      state.stage = "completed";
      await stage("completed");
      const mask = view.getByRole("checkbox", { name: l.mask }),
        exp = view.getByRole("checkbox", { name: l.exp });
      await mask.check();
      await exp.check();
      await expect(view.getByText(l.reject, { exact: false })).toBeVisible();
      await expect(button(l.apply)).toBeDisabled();
      await expect(view.getByRole("heading", { name: l.budget })).toBeVisible();
      await capture("manual");
      await checkAccessibility(page, ".na-page");
      await exp.uncheck();
      state.conflict = true;
      await button(l.apply).click();
      await expect(view.getByRole("alert")).toBeVisible();
      await expect(mask).toBeChecked();
      await button(l.apply).click();
      await expect(view.locator(".na-applied b")).toHaveText(l.manual);
      await radio(l.auto).check();
      await expect(view.getByRole("checkbox")).toHaveCount(0);
      await button(l.apply).click();
      await expect(view.locator(".na-applied b")).toHaveText(l.auto);
      const live = view
        .getByRole("row", { name: new RegExp(l.mask) })
        .getByRole("cell")
        .last();
      await expect(live).toContainText("CPU × 1");
      await expect(live).toContainText(device.model + " × 2");
      await expect(view.getByText(l.fallback, { exact: true })).toBeVisible();
      await capture("auto");
      state.releasing = true;
      await expect(live).toContainText(
        (lang === "en" ? "Releasing " : "Освобождение ") + device.model,
      );
      await expect(live).toContainText(device.model + " × 1");
      await page.setViewportSize({ width: 320, height: 760 });
      expect(
        await page.evaluate(() => document.documentElement.scrollWidth),
      ).toBeLessThanOrEqual(320);
      state.offline = true;
      await expect(view.locator(".na-panel .na-warning").first()).toBeVisible();
      await radio(l.cpu).check();
      await button(l.apply).click();
      await expect(view.locator(".na-applied b")).toHaveText(l.cpu);
      expect(state.writes).toEqual([
        { mode: "manual", gpu_tasks: ["live_mask"] },
        { mode: "auto" },
        { mode: "cpu_only" },
      ]);
    },
  );

for (const lang of ["en", "ru"]) {
  test(
    "Node navigation fits phones and fills the desktop sidebar in " + lang,
    async ({ page }, info) => {
      await nodeApi(page, "auto");
      await page.setViewportSize({ width: 320, height: 800 });
      await page.goto(
        "/iframe.html?viewMode=story&id=workspace--node&globals=language:" +
          lang,
      );
      const nav = page.locator("#workspace-navigation");
      const links = nav.locator(".ws-nav-section > a");
      const node = nav.getByRole("link", {
        name: labels[lang].node,
        exact: true,
      });
      await expect(links).toHaveCount(7);
      await links.nth(4).focus();
      await page.keyboard.press("Tab");
      await expect(node).toBeFocused();
      await page.keyboard.press("Enter");
      await expect(node).toHaveAttribute("aria-current", "page");
      await expect(page.locator(".na-assign tbody tr")).toHaveCount(7);
      await page.evaluate(() => document.fonts.ready);
      for (const width of [
        320, 360, 376, 377, 390, 481, 513, 576, 577, 753, 993, 1199,
      ]) {
        await page.setViewportSize({ width, height: 800 });
        await expect
          .poll(() => nav.evaluate((el) => el.scrollWidth - el.clientWidth))
          .toBeLessThanOrEqual(1);
        const rows = await links.evaluateAll((items) =>
          items.map((item) => {
            const rect = item.getBoundingClientRect();
            const label = item.querySelector("span").getBoundingClientRect();
            return {
              top: rect.top,
              left: rect.left,
              right: rect.right,
              width: rect.width,
              height: rect.height,
              labelFits:
                label.width > 0 &&
                label.left >= rect.left &&
                label.right <= rect.right,
            };
          }),
        );
        expect(new Set(rows.map((row) => row.top)).size).toBe(1);
        expect(rows[5].top).toBe(rows[4].top);
        expect(rows[5].top).toBe(rows[6].top);
        expect(rows[5].left).toBeGreaterThanOrEqual(rows[4].right);
        expect(rows[6].left).toBeGreaterThanOrEqual(rows[5].right);
        for (const row of rows) {
          expect(row.labelFits).toBe(true);
          expect(Math.round(row.left)).toBeGreaterThanOrEqual(0);
          expect(Math.round(row.right)).toBeLessThanOrEqual(width);
          if (width <= 752)
            expect(Math.min(row.width, row.height)).toBeGreaterThanOrEqual(44);
        }
        if (
          lang === "ru" &&
          info.project.name === "chromium-light" &&
          [320, 390].includes(width)
        )
          await nav.screenshot({
            path: "/results/workspace-nav-" + width + ".png",
          });
      }

      await page.setViewportSize({ width: 320, height: 800 });
      const largeText = await page.addStyleTag({
        content: "html { font-size: 200% }",
      });
      await expect
        .poll(() => nav.evaluate((el) => el.scrollWidth - el.clientWidth))
        .toBeLessThanOrEqual(1);
      expect((await links.last().boundingBox()).y).toBeGreaterThan(
        (await links.first().boundingBox()).y,
      );
      await largeText.evaluate((style) => style.remove());
      const column = page.locator(".ws-sidebar-column");
      const controls = page.locator(".ws-navigation");
      const toggle = page.locator(".ws-sidebar-toggle");
      for (const height of [1920, 620]) {
        await page.setViewportSize({ width: 1200, height });
        await expect(toggle).toBeVisible();
        for (const expanded of [true, false]) {
          await expect(toggle).toHaveAttribute(
            "aria-expanded",
            String(expanded),
          );
          const shell = await page.locator(".ws-shell").boundingBox();
          const rail = await column.boundingBox();
          expect(rail.y).toBe(shell.y);
          expect(rail.height).toBe(shell.height);
          expect(rail.height).toBeGreaterThanOrEqual(height);
          if (height === 620) expect(rail.height).toBeGreaterThan(height);
          await page.evaluate(() =>
            window.scrollTo(0, document.documentElement.scrollHeight),
          );
          await expect
            .poll(async () => (await controls.boundingBox()).y)
            .toBeCloseTo(0, 0);
          expect((await controls.boundingBox()).height).toBeCloseTo(height, 0);
          await expect(toggle).toBeInViewport();
          await toggle.click();
          await page.evaluate(() => window.scrollTo(0, 0));
        }
      }
    },
  );
}
