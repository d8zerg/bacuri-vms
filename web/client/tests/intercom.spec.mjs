import { test as base, expect } from "./browser.mjs";

import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const test = base.extend({
  launchOptions: [
    async ({ browserName }, use) => {
      if (browserName === "chromium") {
        await use({
          args: [
            "--use-fake-device-for-media-stream",
            "--use-fake-ui-for-media-stream",
          ],
        });
        return;
      }
      const runtime = mkdtempSync(join(tmpdir(), "bacuri-audio-"));
      const env = {
        ...process.env,
        PULSE_RUNTIME_PATH: runtime,
        XDG_CONFIG_HOME: runtime,
      };
      try {
        execFileSync(
          "pulseaudio",
          [
            "--start",
            "-n",
            "--exit-idle-time=-1",
            "--load=module-null-sink",
            "--load=module-native-protocol-unix auth-anonymous=1",
          ],
          { env },
        );
        try {
          await use({
            env: {
              ...process.env,
              PULSE_SERVER: "unix:" + join(runtime, "native"),
            },
            firefoxUserPrefs: {
              "media.navigator.streams.fake": true,
              "media.navigator.permission.disabled": true,
            },
          });
        } finally {
          execFileSync("pulseaudio", ["--kill"], { env });
        }
      } finally {
        rmSync(runtime, { recursive: true, force: true });
      }
    },
    { scope: "worker" },
  ],
});

test("Microphone sends sampled speech and releases its track on mute", async ({
  page,
}) => {
  await page.addInitScript(() => {
    const NativeNode = window.AudioWorkletNode;
    window.AudioWorkletNode = class extends NativeNode {
      constructor(context, name, options) {
        super(context, name, options);
        this.port.addEventListener("message", ({ data }) => {
          const samples = new Int16Array(data);
          if (samples.length === 1600 && samples.some((n) => Math.abs(n) > 100))
            window.testSpeech = true;
        });
        this.port.start();
      }
    };
    const acquire = navigator.mediaDevices.getUserMedia.bind(
      navigator.mediaDevices,
    );
    Object.defineProperty(navigator.mediaDevices, "getUserMedia", {
      value: async (constraints) => {
        const stream = await acquire(constraints);
        window.testTrack = stream.getAudioTracks()[0];
        return stream;
      },
    });
  });
  await page.goto("/iframe.html?viewMode=story&id=workspace--intercom");
  await page.getByRole("button", { name: "Говорить" }).click();
  await expect
    .poll(() => page.evaluate(() => !!window.testSpeech), { timeout: 10000 })
    .toBe(true);
  await page.getByRole("button", { name: "Выключить микрофон" }).click();
  await expect
    .poll(() => page.evaluate(() => window.testTrack.readyState))
    .toBe("ended");
});
