import assert from "node:assert/strict";
import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { chromium } from "playwright";

const write = process.argv[2] === "--write";
const directory = write ? process.argv[3] : "/usr/share/design/bacuri/brand";

async function output(name, bytes) {
  const path = join(directory, name);
  if (write) await writeFile(path, bytes);
  else assert.ok(bytes.equals(await readFile(path)), name + " differs from the SVG artwork");
}

function ico(frames) {
  const header = Buffer.alloc(6 + 16 * frames.length);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(frames.length, 4);
  let offset = header.length;
  for (const [index, { size, png }] of frames.entries()) {
    const entry = 6 + 16 * index;
    header[entry] = header[entry + 1] = size;
    header.writeUInt16LE(1, entry + 4);
    header.writeUInt16LE(32, entry + 6);
    header.writeUInt32LE(png.length, entry + 8);
    header.writeUInt32LE(offset, entry + 12);
    offset += png.length;
  }
  return Buffer.concat([header, ...frames.map(({ png }) => png)]);
}

const browser = await chromium.launch();
try {
  const page = await browser.newPage({ deviceScaleFactor: 1 });
  const frames = [];
  for (const size of [16, 32, 48, 180, 192, 512]) {
    const source = size === 16 ? "favicon.svg" : "mark.svg";
    const svg = await readFile(join(directory, source), "utf8");
    const apple = size === 180;
    await page.setViewportSize({ width: size, height: size });
    await page.setContent(`<style>
      body { margin: 0; }
      svg { display: block; width: 100vw; height: 100vh; }
      ${apple ? "svg > rect { rx: 0; ry: 0; }" : ""}
    </style>${svg}`);
    const png = await page.screenshot({ omitBackground: true });
    await output(apple ? "apple-touch-icon.png" : `icon-${size}.png`, png);
    if (size <= 48) frames.push({ size, png });
  }
  await output("favicon.ico", ico(frames));
} finally {
  await browser.close();
}
console.log(write ? "Brand exports regenerated." : "Brand exports match the SVG artwork.");
