// Drive the module under Node and say whether it drew anything.
//
//     . /home/rok/src/emsdk/emsdk_env.sh
//     node port/wasm/test/smoke.mjs build-wasm [outdir]
//
// This project has no test suite -- the compiler and the board are the
// feedback loop -- and a wasm module that only a browser can exercise is one
// that nobody checks between releases. This is the smallest thing that is
// still worth running. It walks the whole API and looks for the mistakes that
// would otherwise be found by eye, or not at all:
//
//   - **nothing was drawn.** A pixmap that is empty everywhere means the
//     parameter never resolved, the formatter was not installed, or the draw
//     units never ran. It looks exactly like a correct transparent background
//     until you put something behind it.
//   - **nothing was left clear.** A pixmap with no transparent pixel means the
//     background was filled opaque, which would make every widget unplaceable.
//   - **the alpha is premultiplied.** ImageData is straight alpha. Skipping
//     the division darkens every antialiased edge, which reads as a slightly
//     grubby widget rather than as a bug.
//   - **a pushed parameter did not arrive.** setParameter() is the call that
//     carries an aircraft's real bands in, so the test builds a ParamItem
//     flatbuffer (see parambuilder.mjs) and checks that its bands reach both
//     the listing and the pixels.
//
// With an output directory it also writes a PNG per kind, which is how a human
// checks the part no assertion can: that the widget looks like the panel's.

import { writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { resolve } from 'node:path';

import { buildParamItem, Color } from './parambuilder.mjs';

const buildDir = process.argv[2] ?? 'build-wasm';
const outDir   = process.argv[3] ?? null;

const KINDS = ['Arc', 'BarH', 'BarV', 'Value'];
const W = 200, H = 180;

// -- a minimal PNG writer ---------------------------------------------------
//
// Only so that the pixmaps can be looked at. Node has zlib; the rest is the
// three chunks a truecolour-with-alpha PNG needs.

function crc32(buf) {
  let c, table = [];
  for(let n = 0; n < 256; n++) {
    c = n;
    for(let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    table[n] = c >>> 0;
  }
  let crc = 0xFFFFFFFF;
  for(const b of buf) crc = table[(crc ^ b) & 0xFF] ^ (crc >>> 8);
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

function chunk(type, data) {
  const len  = Buffer.alloc(4);            len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc  = Buffer.alloc(4);            crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
}

function writePng(path, rgba, w, h) {
  // One filter byte (0, "none") in front of each row, which is what the raw
  // scanline format is.
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for(let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    Buffer.from(rgba.buffer, rgba.byteOffset + y * w * 4, w * 4)
      .copy(raw, y * (w * 4 + 1) + 1);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8;    // bit depth
  ihdr[9] = 6;    // truecolour with alpha
  writeFileSync(path, Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ]));
}

// -- the test ---------------------------------------------------------------

let failures = 0;
const check = (bOk, ssWhat) => {
  console.log(`${bOk ? '  ok  ' : '  FAIL'}  ${ssWhat}`);
  if(!bOk) failures++;
};

const KalediItem = (await import(resolve(buildDir, 'kaledi-item.js'))).default;
const mod = await KalediItem();
const r   = new mod.Renderer();

// -- parameters --

const iDefaults = r.loadDefaults();
check(iDefaults > 0, `loadDefaults() gave ${iDefaults} parameters`);

const params = JSON.parse(r.getParameters());
check(params.length === iDefaults, `getParameters() listed ${params.length} of them`);


const rpm = params.find(p => p.id === 500);          // can::Id::EngineRPM_1
check(rpm !== undefined, 'the list holds EngineRPM_1 (500)');
check(rpm?.bands > 0, `EngineRPM_1 has ${rpm?.bands} bands, ${rpm?.low}..${rpm?.high}`);

// -- a pushed parameter, the headline call --
//
// The same flatbuffer a Kanardia tool sends a unit over CAN, and the same one
// ParamStorage::GetParameterFB() writes. This is the path that carries an
// aircraft's real bands into the editor, so it is the one worth building a
// blob by hand for.

const rpmBands = {
  low: 0,
  stops: [
    { value: 1400, color: Color.NoColor },   // below idle: no arc, as the panel has it
    { value: 2500, color: Color.Green   },
    { value: 2800, color: Color.Yellow  },
    { value: 3000, color: Color.Red     },
  ],
};

const blob = buildParamItem({
  canId: 500,
  names: { long: 'Engine RPM', short: 'RPM', tiny: 'RPM' },
  bands: rpmBands,
});

check(r.setParameter(blob) === 500, `setParameter() took a ${blob.length}-byte ParamItem`);

const pushed = JSON.parse(r.getParameters()).find(p => p.id === 500);
check(pushed?.bands === rpmBands.stops.length,
      `the pushed bands stuck: ${pushed?.bands} bands, ${pushed?.low}..${pushed?.high}`);
check(pushed?.high === 3000, 'the top of the range is the blob\'s, not the default\'s');

check(r.setParameter(new Uint8Array([1, 2, 3, 4])) === 0, 'a blob that is not a ParamItem was refused');
check(r.getLastError().length > 0, `  ...saying: ${r.getLastError()}`);

// -- values --

check(r.setValue(500, 2400) === true, 'setValue(500, 2400) took');
check(r.setValue(1, 0) === false, 'setValue on an id nobody holds was refused');

// -- a bad request is refused, not drawn --

check(r.render('{"kind":"Nope","id":500,"w":10,"h":10}') === null, 'an unknown kind was refused');
check(r.getLastError().length > 0, `  ...saying: ${r.getLastError()}`);
check(r.render('not json') === null, 'bad JSON was refused');
check(r.setStyle('{"fThickness":"fat"}') === false, 'a badly typed style field was refused');

// -- the pixmaps --

if(outDir) mkdirSync(outDir, { recursive: true });

for(const kind of KINDS) {
  const px = r.render(JSON.stringify({ kind, id: 500, w: W, h: H }));
  if(px === null) { check(false, `${kind}: ${r.getLastError()}`); continue; }

  check(px.length === W * H * 4, `${kind}: ${px.length} bytes back`);

  // Not `alpha === 255`: ThorVG blending a full-opacity fill onto a cleared
  // canvas lands on 254, one short, which is invisible and would make an
  // exact test fail on every pixel it draws.
  let uSolid = 0, uClear = 0;
  for(let i = 3; i < px.length; i += 4) {
    if(px[i] === 0)   uClear++;
    if(px[i] >= 250)  uSolid++;
  }
  const uPixels = W * H;
  check(uSolid > uPixels / 100, `${kind}: drew something (${(100 * uSolid / uPixels).toFixed(1)}% solid)`);

  // The default style puts a rounded plate behind the whole item, so the only
  // transparency it leaves is the four corners. That the background is
  // genuinely clear is what the no-plate check further down is for.
  check(uClear > 0, `${kind}: rounded the plate's corners (${uClear} clear pixels)`);

  if(outDir) {
    const path = resolve(outDir, `item-${kind}.png`);
    writePng(path, px, W, H);
    console.log(`        wrote ${path}`);
  }
}

// -- a style with no plate is genuinely emptier than one with --

const Corners = px => [0, (W - 1) * 4, (H - 1) * W * 4, (H * W - 1) * 4].map(i => px[i + 3]);

r.setStyle(JSON.stringify({ uPlateRgb: 'none', uBorderRgb: 'none' }));
const bare = r.render(JSON.stringify({ kind: 'Arc', id: 500, w: W, h: H }));
check(bare !== null && Corners(bare).every(a => a === 0), 'with no plate, the corners are transparent');

let uBareClear = 0;
for(let i = 3; i < bare.length; i += 4) if(bare[i] === 0) uBareClear++;
check(uBareClear > W * H / 2,
      `with no plate, most of it is transparent (${(100 * uBareClear / (W * H)).toFixed(1)}%)`);

// Straight alpha, not premultiplied: ImageData is straight, and a white pixel
// that came back at its own alpha would darken every antialiased edge in the
// editor. White on nothing, so anything drawn has to read near 255.
r.setStyle(JSON.stringify({ uPlateRgb: '#FFFFFF', uBorderRgb: 'none', uTrackRgb: 'none' }));
const white = r.render(JSON.stringify({ kind: 'Value', id: 500, w: W, h: H }));
let uEdges = 0, uPremultiplied = 0;
for(let i = 0; i < white.length; i += 4) {
  const a = white[i + 3];
  if(a > 20 && a < 200) { uEdges++; if(white[i] < 250) uPremultiplied++; }
}
check(uEdges > 0, `found ${uEdges} antialiased edge pixels to check`);
check(uPremultiplied === 0, `alpha is straight, not premultiplied (${uPremultiplied} bad of ${uEdges})`);

// -- the coloured bands actually reach the pixels --
//
// The pushed parameter is green from 1400 to 2500 and red above 2800. An arc
// standing in each should put a visible amount of that colour on the canvas;
// that is what says DrawBands() got the blob's bands rather than a default's
// single uncoloured one.

r.setStyle(JSON.stringify({ uPlateRgb: 'none', uBorderRgb: 'none' }));

const CountHue = (px, pick) => {
  let n = 0;
  for(let i = 0; i < px.length; i += 4)
    if(px[i + 3] > 128 && pick(px[i], px[i + 1], px[i + 2])) n++;
  return n;
};
const isGreen = (r_, g, b_) => g > 120 && r_ < 110 && b_ < 110;
const isRed   = (r_, g, b_) => r_ > 170 && g < 110 && b_ < 110;

const arcAt = v => {
  r.setValue(500, v);
  return r.render(JSON.stringify({ kind: 'Arc', id: 500, w: W, h: H }));
};

const green = CountHue(arcAt(2000), isGreen);
const red   = CountHue(arcAt(2900), isRed);
check(green > 100, `the green band is on the canvas (${green} px)`);
check(red   > 100, `the red band is on the canvas (${red} px)`);

if(outDir) {
  writePng(resolve(outDir, 'item-Arc-bands.png'), arcAt(2900), W, H);
  console.log(`        wrote ${resolve(outDir, 'item-Arc-bands.png')}`);
}

r.delete();

console.log(failures === 0 ? '\nall good' : `\n${failures} failed`);
process.exit(failures === 0 ? 0 : 1);
