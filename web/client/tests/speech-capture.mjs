import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { Session } from "node:inspector/promises";
import { pathToFileURL } from "node:url";
import { runInNewContext } from "node:vm";

// The audio worklet runs outside the page profiler. Exercise its delivered script
// with the render quantum and transferable message port used by Web Audio.
export async function speechCaptureCoverage() {
  const source = readFileSync(
    "/results/client/assets/speech-capture.js",
    "utf8",
  );
  assert.equal(source, readFileSync("public/assets/speech-capture.js", "utf8"));
  const url = pathToFileURL("public/assets/speech-capture.js").href;
  const profiler = new Session();
  profiler.connect();
  try {
    await profiler.post("Profiler.enable");
    await profiler.post("Profiler.startPreciseCoverage", {
      callCount: true,
      detailed: true,
    });
    for (const sampleRate of [44100, 48000]) {
      const packets = [];
      let Processor;
      runInNewContext(
        source,
        {
          sampleRate,
          AudioWorkletProcessor: class {
            port = {
              postMessage(buffer, transfer) {
                packets.push(
                  new Int16Array(structuredClone(buffer, { transfer })),
                );
              },
            };
          },
          registerProcessor(name, constructor) {
            assert.equal(name, "speech-capture");
            Processor = constructor;
          },
        },
        { filename: url },
      );
      const capture = new Processor();
      assert.equal(capture.process([]), true);
      for (const [input, expected] of [
        [-2, -32767],
        [0.5, 16384],
        [2, 32767],
      ]) {
        const frameCount = sampleRate / 5;
        for (let i = 0; i < frameCount; i += 128) {
          const block = new Float32Array(Math.min(128, frameCount - i)).fill(
            input,
          );
          assert.equal(capture.process([[block]]), true);
        }
        assert.equal(packets.length, 1);
        const packet = packets.shift();
        assert.equal(packet.length, 1600);
        assert.ok(packet.every((sample) => sample === expected));
      }
    }
    const { result } = await profiler.post("Profiler.takePreciseCoverage");
    const scripts = result.filter((script) => script.url === url);
    assert.ok(scripts.length, "Missing audio worklet execution");
    return scripts.map((script) => ({ ...script, source }));
  } finally {
    profiler.disconnect();
  }
}
