# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An LVGL 9 UI with ThorVG vector graphics, in C++, for the **Waveshare
ESP32-P4-WIFI6-Touch-LCD-4C** (4", 720×720 round IPS, 2-lane MIPI-DSI / JD9365,
GT911 touch). There is no test suite and no linter — the compiler and the board
are the feedback loop.

**It is built three times**, from one set of sources:

```
src/          the product. Knows nothing about ESP-IDF, FreeRTOS, SDL or POSIX.
port/esp/     what only the board can answer: the BSP panel, the TWAI
              controller, NVS, the OTA slots. Built by main/CMakeLists.txt,
              which is the ESP-IDF component and nothing else.
port/pc/      the same answers on a desktop: an SDL window, can::CanuCan on a
              CANU adapter, a directory of files, a firmware image on disk.
              Built by port/pc/CMakeLists.txt, a plain CMake project.
port/wasm/    a corner of it, for the Kaledi layout editor: src/Item/ and
              PainterTvg compiled to WebAssembly behind a headless LVGL, with
              no bus, no model and no store. Built by port/wasm/CMakeLists.txt
              with emscripten. See "The Kaledi item renderer" below.
cmake/        the source lists and the font generation all three builds share.
```

`src/Platform.h` is the seam -- logging, time, the LVGL lock, threads, the host
link, heap figures -- plus three interfaces with a lifetime of their own:
`app::CanPort` (src/CanPort.h), `app::BlobStore` (src/BlobStore.h) and
`platform::FirmwareTarget`. Each port supplies one implementation of each.

The simulator is not a mock. It runs the real scenes through the real ThorVG,
the real `avio::ModelBase`, the real CANaerospace stack and the same
one-character console, so anything that is not the panel, the controller or the
flash can be developed and screenshotted without a board on the desk.

## Commands

The build also needs **lv_font_conv**, LVGL's own font converter, because the
Kanardia fonts are generated from the TTF at build time. Install it once:

```bash
npm install --prefix tools lv_font_conv
```

Every command needs the IDF environment sourced first; it is **not** on `PATH` by default:

```bash
. ~/esp/v5.5.1/esp-idf/export.sh
```

Or skip all of that and use the run skill, which wraps build/flash/drive:

```bash
python3 .claude/skills/run-espp4/driver.py smoke
python3 .claude/skills/run-espp4/driver.py shot --scene gauge --out gauge.png
```

**The simulator needs none of the IDF environment.** It is a plain CMake
project; it wants `libsdl2-dev`, Paho for the cloud client
(`libpaho-mqtt-dev libpaho-mqttpp-dev`) and the same `lv_font_conv`:

```bash
cmake -S port/pc -B build-sim -G Ninja
cmake --build build-sim
./build-sim/espp4-sim                       # --can /dev/ttyUSB0, --state DIR
```

The run skill drives it through the same console, with `--sim`:

```bash
python3 .claude/skills/run-espp4/driver.py --sim build
python3 .claude/skills/run-espp4/driver.py --sim smoke
python3 .claude/skills/run-espp4/driver.py --sim shot --scene rpm --out rpm.png
```

**The wasm module needs none of it either**, only emscripten -- already
installed and activated at `/home/rok/src/emsdk`, and not on `PATH` by default:

```bash
. /home/rok/src/emsdk/emsdk_env.sh
emcmake cmake -S port/wasm -B build-wasm -G Ninja
cmake --build build-wasm                    # -> kaledi-item.js + .wasm
node port/wasm/test/smoke.mjs build-wasm [outdir]   # 33 checks, PNGs with outdir
python3 -m http.server -d build-wasm        # then open / for the demo page
```

The smoke test builds a real `ParamItem` flatbuffer to push, so it wants one
more tool: `npm install --prefix tools flatbuffers`.

LVGL and `lvgl_cpp` come from `managed_components/`, so the simulator compiles
the very sources the board does. That directory is resolved by the IDF
component manager and is not tracked; in a checkout that has never run
`idf.py reconfigure`, the simulator's CMake fetches both at the pinned
versions instead.

Formatting and new source files go through the kanardia-style skill, which
needs `clang-format-20` (`sudo apt install clang-format-20`):

```bash
python3 .claude/skills/kanardia-style/style.py check    # non-zero if anything is off
python3 .claude/skills/kanardia-style/style.py format
python3 .claude/skills/kanardia-style/style.py new Foo  # banded src/Foo.h + src/Foo.cpp
python3 .claude/skills/kanardia-style/style.py new Foo -d port/pc   # or in a port
```

```bash
idf.py set-target esp32p4        # first time only; also re-resolves components
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # Ctrl+] exits monitor
idf.py -p /dev/ttyACM0 app-flash monitor   # app only, faster iteration
idf.py menuconfig                # inspect effective config interactively
```

After editing `sdkconfig.defaults`, delete `sdkconfig` and rebuild — defaults are
only consumed when `sdkconfig` does not exist:

```bash
rm -f sdkconfig && idf.py build
```

**Always verify config changes actually landed.** Unknown Kconfig symbols in
`sdkconfig.defaults` are silently ignored, so a setting can look applied and not be:

```bash
grep -E "^CONFIG_(SPIRAM_SPEED|LV_USE_THORVG|LV_DRAW)" sdkconfig
```

## Driving the board, or the simulator

The firmware carries a one-character debug console -- USB-Serial/JTAG on the
board, stdin/stdout in the simulator, the same protocol on both
(`src/SerialConsole.cpp`): `i` stats (scene, frame time, heap, the model's
rpm/eng/moving/stack, the CAN counters, the NVS entry count and the internal
heap low-water mark, plus the settings page's level and selection and the cloud
client's state and counters), `t` toggle
scene, the terminal's own arrows / Enter / Esc to drive the settings page
(`m`/`M` are unambiguous aliases for down and Enter), `w` write the option
blobs to NVS, `P` push a parameter at ourselves over CAN, `c` connect the cloud
client or drop it again, `s`/`S` screenshot as
base64 RGB888. `.claude/skills/run-espp4/` documents the protocol and ships
`driver.py`, which is how you smoke-test either build or get a PNG of the panel
without looking at it -- `--sim` picks the simulator. Read that SKILL.md before
touching serial or snapshots -- it lists the traps (port-open resets the chip,
logs corrupt the base64 stream, LVGL's RGB888 is B,G,R).

In the simulator the console is on stdout and the log on stderr, so the two
cannot corrupt each other; `ConsoleOpen()` takes the original stdout for itself
and points the descriptor at stderr, because Common's `PRINTF` goes to `printf`
off the board and would otherwise land in the middle of a base64 body.

Measured on hardware, per scene: **gauge ~60 ms, rpm ~58 ms, ias ~38 ms,
scale ~34 ms, altimeter ~28 ms** per ThorVG frame, against a 33 ms timer -- so the UI runs at
roughly 16 fps on the gauge and hits the timer on the others. **Internal heap:
low-water mark ~32 kB, largest contiguous block ~31 kB** once the three 32 kB
task stacks are placed. Read `heap_int_min` in the console's `i` line, not
`heap_int` -- the latter is a snapshot taken at a random point in a ThorVG
frame and swings between ~35 kB and ~80 kB, which reads like a regression when
it is only sampling noise. That is the tightest resource in the project; the
model task's own stack headroom is `model_stack=`, and app_main logs the
largest block at boot.

**The simulator reports all four heap figures as zero and `model_stack=0`**, on
purpose: a desktop has none of those limits and inventing numbers for them
would invite comparisons that mean nothing. Its frame times are real but are
not the board's -- the same gauge that costs 60 ms on the panel costs about
1.5 ms here, so the simulator says nothing about whether a scene will hit the
33 ms timer.

## Architecture

Three layers, each of which has bitten this project at least once:

**BSP → LVGL.** `port/esp/MainEsp.cpp` does nothing but build a
`bsp_display_cfg_t` and call `bsp_display_start_with_config()`. The Waveshare BSP brings up the DSI panel,
GT911 touch and `espressif/esp_lvgl_adapter`, which owns the LVGL task and the
tear-avoidance frame buffers. Everything after that runs under the LVGL lock:
`bsp_display_lock()` / `bsp_display_unlock()`.

**LVGL → ThorVG.** ThorVG is *inside* LVGL (`managed_components/lvgl__lvgl/src/libs/thorvg`),
not a separate component. It is reached through the `lv_draw_vector_*` C API,
enabled by `CONFIG_LV_USE_VECTOR_GRAPHIC` + `CONFIG_LV_USE_THORVG` +
`CONFIG_LV_USE_THORVG_INTERNAL`.

**C API → C++ binding.** `lvgl_cpp` (git dependency, not on the component
registry) wraps LVGL as `lvgl::Canvas`, `lvgl::Label`, `lvgl::Timer`,
`lvgl::VectorDraw`, `lvgl::VectorPath`. `VectorScene.cpp` is written entirely
against the binding; `dsc.raw()` is the supported escape hatch to the C layer
where the binding has a gap.

**Kanardia Common → the scale.** `cmake/KanardiaSources.cmake` lists a
hand-picked subset of the shared `Public/Common` tree (parameter bands, units,
scale utils) straight out of the working copy at `$ENV{HOME}/Branch/v4_3`,
exactly the way `Private/Horis/v1/CMakeLists.txt` does, and both builds compile
that one list. Override the location with `-DKANARDIA_BRANCH=/path/to/v4_3`;
the build fails loudly if the tree is missing. `ScaleDraw.cpp` against
`PainterTvg` is the ThorVG twin of
`Common/Scale/ScaleDrawQt.cpp` — same structure, same style structs, LVGL vector
paths instead of a QPainter. Two things to know before touching it:

- Common assumes Qt in a couple of leaf spots. `KanardiaCommon.h` supplies a
  constexpr `qRgb()` so `Parameter/ParamColors.h` compiles here; include it
  *before* anything from `Parameter/`, and leave the shared tree alone.
- **Scale colours are opaque, alpha byte ignored.** `C32_WHITE` and friends are
  plain RGB with a zero alpha byte, and Qt reads them through `QColor(QRgb)`,
  which is opaque. Honour the byte and every white dash and label renders
  invisible.
- **`int32_t` is `long` here, and that breaks two things.** `Map/MapBase.h` does
  `assert(common::IsInside(iLon, -180, 179))`; `IsInside` deduces one `T` from
  all three arguments, so `long` against `int` fails to deduce and the header
  will not compile. `src/KanardiaCommon.h` adds a constrained mixed-type
  overload, and `cmake/KanardiaSources.cmake` force-includes it into every
  Common source with `-include` -- which is also where `<cstring>` and a
  printf-style `qDebug()` come from, both for `CanPort/CanuCan.cpp`, which the
  simulator builds and which assumes Qt's headers got there first. The same mismatch turns Common's `%u` formats into
  `-Werror=format` failures, hence `-Wno-format` on those sources. Upstream both
  want fixing properly (`common::IsInside<int32_t>(...)`, `PRIu32`).
- **Two Common sources need exceptions back.** `BLOB/BLOBPackUnpack.cpp` and
  `Compress/CompressZeros.cpp` `throw` on malformed input, which will not
  compile under IDF's global `-fno-exceptions`. On the board they are built
  with `-fexceptions`; the throwing branches are unreachable here, and would
  terminate if they ever fired. The desktop has exceptions anyway. `-DNO_LZO_COMPRESSION` (Common's own switch)
  keeps miniLZO out of the image.

**Fonts are generated, not checked in.** `cmake/KanardiaFonts.cmake` runs
`Public/Font/Kanardia.ttf` through `lv_font_conv` at build time -- LVGL's own
pipeline, the same tool `scripts/built_in_font/` uses for Montserrat -- and
emits one `.c` per size into the build tree plus a generated `KanardiaFont.h`
holding the `LV_FONT_DECLARE`s and a `KANARDIA_FONT_LIST(X)` X-macro. Change
`KANARDIA_FONT_SIZES` and the header follows; nothing else needs touching,
because `Painter::CreateFont()` builds its lookup table from that macro.

The font carries Kanardia's unit glyphs in the private-use area, so
`KANARDIA_FONT_RANGES` exports `0xE000-0xE118` alongside ASCII. The demo scene
shows a row of them as proof they survived the conversion. The Montserrat
fonts are still enabled in `sdkconfig` because `LV_FONT_DEFAULT` points at one;
nothing in this project draws with them any more.

**Values are printed through Common's own formatting layer.** `Avio/Format/AvioFormat.h`
is the entry point: it holds the one process-wide `unit::Formatter*`, which
`src/App.cpp` installs with `avio::format::SetUnitFormatter()` before the scene
is built. Everything below reaches it from there -- `avio::format::ToString()`,
`Formatter::FormatAzimuth()`, the `Parameter` overloads -- so no call site
carries a formatter. `unit::FormatterUtf8` is the one we install; it maps a
`unit::Key` onto the private-use codepoint `Common/UserTTF.h` assigns it, so
`km/h` reaches the panel as one stacked glyph, not four characters.
`unit::Convert()` does the unit arithmetic, which is why no 3.6 or 3.28084
constants appear in the scenes any more: the model holds SI, the scales ask for
`km_h` and `feet`.

The numbers go through `parameter::Format(fUser, Function, Key)`, which is where
`avio::format`'s own `Parameter` overloads send theirs -- so rpm and altitude
round to the nearest ten and airspeed to the unit, the same as every other
Kanardia product. `VectorScene.cpp`'s file-local `Readout()` is just those two
calls glued together.

Four things to know:

- **`SetUnitFormatter()` must be called before anything formats.** `GetUnitFormatter()`
  asserts, and everything below it goes through that one pointer.
- **`Common/Unit/Value.h` opens with `#error "Obsolete"`** and cannot be used.
  The value/unit pairing it provided is now a plain `(float, unit::Key)`.
- **Not every unit has a glyph.** `FormatterUtf8::Format(..., Glyph)` answers
  with an empty string for `RPM`, `percent` and anything else Common never drew
  one for, so `UnitGlyph()` falls back Glyph -> Signature. Never use the
  formatter's result without that fallback.
- **`AvioFormat.cpp`'s `Parameter` overloads survive without `Param.cpp`** only
  because `--gc-sections` drops them; `parameter::Parameter` itself is not built
  here. Call one and the link breaks until `Parameter/Param.cpp` is added.

`main/CMakeLists.txt` also defines **`ESP32`** for the Common sources:
`Common/Defines.h` routes `PRINTF` to `ESP_LOGI` behind `defined(ESP32)`, and
IDF does not define it for the P4. It is the only `ESP32` test in the whole
tree, so it just selects the logging branch Common already intends. The
simulator does not define it, which is why Common's `PRINTF` lands on `printf`
there -- see the note about stdout above.

**Common → the CAN bus.** `app::CanPort` (src/CanPort.h) is what the product
asks of a bus: start, stop, mode, and the counters the console reports.
`port/esp/CanPortEsp` implements it on the P4's TWAI controller;
`port/pc/CanPortCanu` implements it with `can::CanuCan`, Common's own desktop
port for the Kanardia CANU v2 adapter -- the same serial protocol Nesis talks
to a bus through. `CanProcessor` is the CANaerospace side: it
mirrors `can::CANHandler::Process()` -- split incoming messages by id range into
services, status, NOD and special -- without the `CANHandler` template, which
needs a product's whole sender/service stack. NOD messages go into the
`DirectNOD` the model reads; sign-of-life messages go into
`can::uCUnitInfoContainer`, the microcontroller-side unit container.

`src/ApplicationDefines.h` is the extension point Common expects from every
product: our node id and which halves of the optional services we implement.
`USE_CAN_MIS_A` asks other modules to identify themselves and answers nothing,
so the container's "identified" count stays at zero on a bus where nobody
answers -- correct, not a fault. `USE_CAN_DDS_B` and `USE_CAN_MCS_B` are the
receive half of the parameter push described below.

**There is CAN transceiver on this board**, GPIO30, GPIO33, so the port comes
up in `Mode::Normal`. `Mode::SelfTest` exercises the whole path without one:
RX is mapped onto the TX pin so the GPIO matrix loops the signal back, the
controller runs in `TWAI_MODE_NO_ACK` (nobody is there to acknowledge), and
frames are transmitted as self-reception requests (`twai_message_t::self`).
All three are needed -- NO_ACK alone transmits but never receives.

**The simulator means the same thing by `Mode::SelfTest`**, and falls back to
it on its own when there is no CANU adapter on the configured device -- the
usual state on a desk. There the loopback is a queue and a receive thread
rather than a controller, and that is load-bearing: `CanProcessor::Pump()`
holds the service mutex while `OldServices::Update()` posts a message, so
delivering it inline would re-enter `Process()` on the same thread and
deadlock. Going through a thread is also the shape the board has.

In self-test, on either build, `Simulate()` and `SendSignOfLife()` put real
CANaerospace frames on the port and they arrive back through the full decode
path -- which is what makes the simulator's instruments move. `Simulate()` goes
silent in `Mode::Normal`: on a real bus those ids belong to somebody else.

**Common → the flight model.** `AppModel.h/.cpp` derive a concrete
`avio::ModelBase` and tick it on a task of its own -- `platform::StartTask()`,
a FreeRTOS task on the board and a thread in the simulator: `Update50ms()` on a
50 ms beat, `Update1s()` once per second, plus the CAN processor's one-second
work. The beat is kept against `platform::Micros()` rather than by sleeping a
period at a time, so a slow tick is absorbed instead of accumulating. There is
no GNSS receiver on either build, so that hook answers "nothing connected"; the
NOD is fed from the CAN port. Everything above the NOD -- GNSS,
navigation, clock, sunrise/sunset, the above/below detectors behind
`IsFlying()` / `IsEngineRunning()` / `IsMoving()` -- is the unmodified shared
code. The three instrument scenes read rpm, IAS and altitude from the model, and the
console's `i` line carries `rpm=`, `eng=`, `moving=` and `model_stack=` so the
loop can be checked from the host. `SaveLastKnownCoordinate()` is no longer a
stub: it marks the option dirty and lets `Settings::Save()` write it to the
settings store.

Scenes cycle `gauge` -> `scale` (tachometer, `Scale::DrawArc`) -> `ias`
(airspeed, `Scale::DrawArcIAS`) -> `altimeter` (three pointers over a
full-circle `DrawArc`) -> `rpm` (engine and rotor side by side) -> `panel` (a
configured sheet of items rather than one instrument -- see below) -> `menu`
(the settings page, which is not an instrument at all -- see below).
`Arc2D::IsCircle()` is what makes the altimeter drop the label that would
otherwise land on top of its zero.

**The settings page is a screen of its own.** `src/MenuPage.h/.cpp` is a menu
of levels -- units per group, the azimuth reference, the UTC offset, a few
system actions -- and it is a view onto `app::Options` and nothing else: a row
asks the option what it holds every time it is drawn, and a tap hands the next
value straight back. Common already knows which units a group allows
(`parameter::unit_group_util::GetUnits()`) and what the group is called, so the
level table is mostly one line per row.

Four things to know:

- **It is a second `lvgl::Screen`, not a sixth face.** Nothing on it moves, so
  `Scene::NextMode()` pauses the frame timer on the way in and resumes it on
  the way out. A ThorVG frame costs up to 60 ms on this panel and a settings
  page has no business paying that.
- **The title bar is an eye**: the panel's own circle for the upper lid, a much
  shallower arc bulging the other way for the lower one, the two meeting in a
  point at each side, filled with a gradient in the level's accent colour. It
  is drawn on a 720x216 ARGB8888 canvas, and only when the level changes.
  `lv_vector_path_append_arc()` starts a subpath of its own, so the two lids
  cannot be two arcs -- they would fill as two separate shapes; the outline is
  walked as line segments instead.
- **The page never loads anybody else's screen.** Leaving the root level calls
  the `menu::CloseHandler` `VectorScene.cpp` registered, which is the same step
  out of `Mode::Menu` a tap on an instrument would make. `menu::CreatePage()`
  is therefore called last in `Scene::Build()`, after `Screen::active()` has
  been taken.
- **Changes are written once, on the way out**, plus on demand from the System
  level. Saving per tap would stall the LVGL task inside a flash write in every
  single row.
- **A tap is acted on from `lv_async_call()`, not from the click itself.**
  Entering a submenu rebuilds the rows, and the row being tapped is one of the
  objects that would be deleted -- while LVGL is still walking its event list.
- **The page carries a selection a finger never moves.** It rests on the title
  bar, where it marks nothing, until a key steps it: up/left back, down/right
  on, Enter activates, Esc goes up a level -- and out of the page at the root.
  That is also the only way to reach a submenu or a value from the host.
- **Keys arrive through an LVGL group, because there is no other route.** A
  keypad input device with no group delivers a key to nothing at all, so the
  page keeps a group holding one object -- its own screen -- purely as
  somewhere for LVGL to deliver `LV_EVENT_KEY`; the group's focus never moves,
  the page's selection does. It points every keypad and encoder device at that
  group while it is up and away from it when it is not, so the simulator's SDL
  keyboard drives it as it stands and a knob would drive it on the board
  (`lv_group_set_editing()` is what makes an encoder send its two directions as
  keys instead of walking the focus). `menu::HandleKey()` is the entry point,
  and it takes `menu::Key`, not `LV_KEY_*`, so the console does not have to
  include LVGL to say "down".

**The dual tachometer is two ordinary `DrawArc()` calls.** What makes the ")("
layout is only where the two `Arc2D` centres sit and which way the spans run:
each centre is pushed `R + GAP` outboard along the horizontal, so just the near
flank of each circle lands on the panel -- the right flank of the left circle
and the left flank of the right one. Both spans run bottom to top, so the two
scales mirror. Because Common measures the style's dash and label offsets
inward, towards the arc's own centre, the mirroring puts both sets of dashes
and labels on the *outer* edges for free, with the coloured bands facing each
other across the middle.

Two consequences worth knowing:

- **No needles.** A needle has to be rooted at its arc's centre, which here is
  outboard of the panel edge. `Scene::DrawArcMarker()` draws a triangle riding
  the scale instead, sitting just off the convex side and pointing back at the
  dashes -- which is what a side-by-side tachometer reads like anyway.
- **The readout boxes are colour-coded**, pink for engine and teal for rotor,
  matching their markers. Both boxes say "RPM" and the numbers alone would not
  say which scale they belong to. That is also the one place `m_valueLabel`
  changes style by scene: this face needs two smaller boxes side by side where
  every other one uses a single centred box at the full size.

**A scale's labels are always white**, whatever the pen -- `DrawTextAsPath()`
in the Qt original forces white too, and without it `DrawArcIAS` leaves a red
pen behind after the Vne dash and every label comes out red. White is the
*default* of `PainterTvg::DrawTextAnchored()`, not a rule the back end
enforces: `src/Item/` letters a readout in the colour of the band its pointer
is standing in, which is the only place that argument is ever passed.

**`src/Item/` is a sheet of items, drawn in two passes.** An item is one
parameter inside one box -- `item::Arc`, `item::BarH`, `item::BarV`,
`item::Value` -- and it is `lasky::utils::Arc` and its siblings from
`Public/Nesis` rewritten against `PainterTvg`.

`item::Base` is two pure virtuals and nothing else: `DrawStatic(Painter&)` and
`DrawDynamic(Painter&)`. An item is built for one row of the layout and keeps
that row's parameter, box and style for its whole life, so there is nothing to
hand it per frame and nothing for the panel to branch on. What the kinds share
sits below `Base` as free functions in `item` (`DrawPlate()`, `DrawBands()`,
`DrawPointer()`, `FitName()`, the readout pair); what only one kind needs is
that kind's own business -- an arc works out where its centre goes, a bar does
not care. `item::Panel` is what makes the split pay:

- **the static half is rendered once**, into a background `lvgl::DrawBuf` of
  its own: the plates, the bare track, the coloured bands, every title;
- **every frame, that whole buffer is blitted over the canvas**
  (`lv_draw_buf_copy()`) and only the pointers and the readouts are drawn on
  top of it.

That is the whole reason a panel of a dozen items is affordable here. A ThorVG
frame costs tens of milliseconds on this board and the count of paths is what
drives it; the bands and the lettering are most of those paths and none of them
move. The price is one more ARGB8888 buffer the size of the canvas, which LVGL
places in PSRAM -- and if there is no room for it, `Scene` drops `Mode::Panel`
out of the cycle the same way it drops the settings page.

Four things to know:

- **A layout is data**, not code: `item::Config` is a kind, a `can::Id` and a
  box, and `Panel::MockupLayout()` is one hardcoded answer standing in until
  the real one comes out of the settings store. `Panel::Rebuild()` turns those
  rows into owned `item::Base`s when the layout or the style is set, which is
  the only place a kind is ever branched on. A row naming an id this unit does
  not hold is dropped there, not an error -- a layout written for a whole panel
  will name plenty of them.
- **An item's parameter pointer is into the container**, and stays valid
  because `ParameterContainer` only ever has parameters *applied to* in place;
  nothing is inserted after `app::Parameters`' constructor. A pushed parameter
  therefore needs no rebuild, only `Invalidate()`.
- **`Panel::Build()` runs after the model loop**, for the same reason the
  scenes cache their bands at build time: the items resolve their parameters
  once, and the container has to exist by then.
- **`Panel::Invalidate()` is not optional.** The bands live in those pixels, so
  a changed layout, a changed style or a parameter pushed over the bus has to
  ask for the static half again. `Scene::RefreshBands()` does.
- **The static pass borrows the canvas.** LVGL draws into whatever buffer the
  canvas is pointed at, so `RenderStatic()` points it at the background one,
  renders, and points it back -- which is why `Render()` is handed the canvas's
  own `lv_draw_buf_t`.
- **Items are not scales.** No dashes and no numbered labels: an item is read
  off the band its pointer is standing in, and a sheet of a dozen has no room
  to letter each one. `scale::Scale` is still what a single instrument face
  wants.

`VectorScene.cpp` holds one file-static `Scene`. Its widgets are
`std::optional<T>` members constructed in `Build()`, because binding objects are
move-only and take ownership of the underlying `lv_obj_t`. A `lvgl::Timer`
re-renders the canvas at ~30 fps.

**Parameters are managed by Common's own container.** `AppParameters.h/.cpp`
derives `app::Parameters` from `parameter::ParameterContainer` -- the hash of
`parameter::Parameter` keyed by `can::Id` that every Kanardia product uses. Each
parameter carries its function, its system unit, the unit the pilot reads, its
colour bands, its names and a low-pass filter, and pulls its own value through a
callback straight out of the `can::DirectNOD`. The three instrument scenes read
values and bands from it, so `VectorScene.cpp` no longer builds bands by hand,
calls model getters, or does unit arithmetic.

Two things to know:

- **Bands are stored in the system unit, not the user unit.**
  `function_util::GetSystemUnit()` says m/s for airspeed, metres for altitude,
  rpm for engine speed; `Parameter::GetUserBands()` converts on the way out.
  That is what makes a parameter blob portable between products showing
  different units, and it is why `AppParameters.cpp` writes its bands in a
  readable unit and calls `Bands::Convert()`.
- **The scene is built after the model loop.** `app_main()` runs
  `StartModelLoop()` before `CreateScene()` precisely because the scenes cache
  `GetUserBands()` at build time, and the container is only populated -- from
  its defaults, then from the stored blob -- once the model exists.

**A node on the bus can push a new parameter at us.** This is the real Kanardia
protocol -- the receive half `Private/Indu` implements and the send half Nesis
drives -- which is why the existing tooling can configure this board:

1. the sender offers a **DDS_BUFFER** (`0x05`) data download, and
   `CanProcessor::AcceptDownload()` takes it if it fits the 1 kB scratch buffer;
2. the bytes arrive one 32-bit register per message, `StoreDownloadMessage()`
   filing each at `GetMessageCode(msg)-1`;
3. an **MCS_APPLY_BUFFER_DATA** (`0x0E`) message commits it. Register B packs
   the length above a CRC-16 (`(size << 16) | crc`) and the data index carries
   a `BufferDownloadCommand`; `Parameter` is the only one we honour.

The buffer is a single `parameter::fbs::ParamItem` flatbuffer -- what
`ParamStorage::GetParameterFB()` produces. `Parameters::ApplyPushedParameter()`
looks its `can_id` up in the container and hands it to
`ParamStorage::ApplyTo()`. A blob naming an id this unit does not show is
ignored, not an error: a tool pushing a whole panel will name plenty of them.

Two things to know:

- **The apply does not happen on the CAN thread.** `ApplyTo()` resizes the
  parameter's value vector, which the LVGL task is sampling every frame. So
  `ConfigureModule()` only publishes the verified bytes, and
  `app::ApplyPushedParameter()` -- called from the scene's tick, on the LVGL
  task -- applies, saves and refreshes the cached bands.
- **A push is saved immediately**, by re-writing the whole container blob. A
  change that survived only until the next power cycle would be worse than no
  change at all.

**Sending goes through the same services Nesis uses.** `DialogParameters::
Transfer()` in `Public/Nesis` is two lines per parameter:

```cpp
auto vFB = ParamStorage::GetParameterFB(pc.Find(pP->GetId()));
if(vFB.empty()==false)
    pU->Download(BufferDownloadCommand::Parameter, vFB);
```

`UnitInfoBase::Download()` behind that is DDS_BUFFER followed by
MCS_APPLY_BUFFER_DATA carrying `(size << 16) | crc16`.
`CanProcessor::PushBuffer()` is that call against the same `OldServices`, which
is why `USE_CAN_DDS_A` and `USE_CAN_MCS_A` are on: the shared state machine
does the handshake, the indices, the checksum and the timeouts, and the only
product-specific part left is `GetDownloadData()` handing out one register at a
time.

Two things differ from the desktop, both because Nesis blocks and we cannot:

- **The commit waits for the transfer.** `UnitInfoBase::Download()` writes the
  download and the apply as consecutive statements; here DDS_A runs on its own,
  so `Pump()` fires the `ConfigureModule()` once the download reaches
  `DDS_A::sSuccess`.
- **`OldServices::Update()` needs a real beat.** It posts at most one download
  message per call, so a once-a-second poke would take two minutes for a
  368-byte parameter. `Pump()` runs on the model task's 50 ms tick and sends
  until the controller's transmit queue is full -- 92 messages in about 600 ms.

**`OldServices` is now locked.** It is a plain state machine with no locking of
its own, reached from the port's receive thread (`Process()`) and the model
task (`Pump()`, `Update1s()`). Before the sending half existed the overlap was
harmless; a half-sent download whose response lands mid-`Update()` is not.

`Model::SimulateParameterPush()` packs one edited parameter and pushes it at our
own node id. In self-test every frame comes back, so the console's `P` command
drives the whole loop for real -- DDS_A, DDS_B, MCS_A, MCS_B, the apply and the
write to the settings store. The tachometer's bands visibly change, and survive
a restart. It runs in the simulator too, which is the cheapest way to exercise
that path.

**The parameter set is saved as one blob.** `Settings::SaveParameters()` /
`LoadParameters()` wrap `parameter::ParamStorage`, which packs the whole
container into a single flatbuffer and LZO-compresses it: 451 bytes for our
three parameters. That is one entry, not one per key -- unlike the options
-- because that packed form is what the rest of the Kanardia tooling reads and
writes, and splitting it would make the blob non-portable. `ParamStorage::Load()`
answers silently on a bad CRC, so `LoadParameters()` proves the blob names at
least one parameter we hold before applying it.

Bringing `ParamStorage` in pulled miniLZO into the image (`LZO/minilzo.c`, built
as C and deliberately outside `KANARDIA_COMMON_SOURCES`, because those get
`-include KanardiaCommon.h`), plus `Param.cpp`, `ParamContainer.cpp`,
`ParamFuelLevel.cpp`, `CanIdDetails.cpp` and `CRC-32.cpp`.

**The cloud client is MQTT, and it is Nesis's client in miniature.**
`src/MqttClient.h/.cpp` is the same shape as `core::cloud::CloudClient` in
`Public/Nesis`, because that is the client the Kanardia server already knows: a
device with no credentials connects as `provision`, claims itself with the
product's own provisioning key and secret, keeps the access token it gets back
and reconnects with it. After that it publishes telemetry every ten seconds and
answers remote calls on `v1/devices/me/rpc/request/+`.

`app::MqttPort` (src/MqttPort.h) is the seam, next to `CanPort` and
`BlobStore`. Neither implementation speaks MQTT itself, which is the point: a
protocol written twice is a product that behaves two ways. `port/esp/MqttPortEsp`
is ESP-IDF's esp-mqtt component, which brings its own task and its own
reconnect; `port/pc/MqttPortPaho` is the Eclipse Paho C++ client
(`sudo apt install libpaho-mqtt-dev libpaho-mqttpp-dev` -- both halves, since
the C++ package does not pull the C one in, and port/pc/CMakeLists.txt says so
when either is missing). Paho's own automatic reconnect is switched off there
and one loop does the dialling, because paho only reconnects after a first
success and a simulator is routinely started before its broker.

Five things to know:

- **The provisioning key and secret are not Nesis's**, and must not become
  Nesis's: the pair is what tells the server which device profile a newly
  claimed unit belongs to. `MQTT_PROVISION_KEY` / `MQTT_PROVISION_SECRET` in
  MqttClient.cpp carry placeholders and are overridable from the build, so a
  real pair never has to live in the repository.
- **Only `sendMessage` and `sendLayout` are answered.** Everything else the
  server asks a Nesis -- the terminal, the logbook, the autopilot -- is counted
  and dropped, the same silence Nesis answers an unknown method with. A message
  lands in the scene's own line for as long as its timeout says; a layout is
  kept in memory, since nothing here renders one yet.
- **The receive thread does nothing but copy.** The port delivers on a thread
  of its own, `OnMessage()` queues the bytes, and every bit of parsing,
  answering and reconnecting happens on the model task in `Pump()` (50 ms) and
  `Update1s()`. That is the rule pushed parameters taught the CAN side, and it
  is also why provisioning can stop and restart the port without deadlocking on
  the thread that is delivering to it.
- **The token is one more blob in the settings store**, under the key `mqtt`,
  in the same JSON shape Nesis keeps in `mqtt.json`. A token stored under a
  device name we no longer answer to is ignored and the unit claims itself
  again.
- **It is off unless a broker is configured**, and says so at boot rather than
  staying silent, which reads as "not in this build". `ESPP4_MQTT_HOST` (`host`
  or `host:port`) names one and is also what starts the client at boot; the
  console's `c` does it by hand against the compiled-in `thing.kanardia.eu`.
  `ESPP4_MQTT_DEBUG=1` adds a line per message received and per method not
  answered -- and raises the logger to Debug on its way past, because asking
  for debug output that the level then drops is the same as no switch at all.

**The board's network is the ESP32-C6 beside the P4.** This chip has no radio.
`port/esp/WifiEsp.cpp` is an ordinary `esp_wifi_*` station -- netif, event
loop, `esp_wifi_connect()`, reconnect on every disconnection -- and
`espressif/esp_wifi_remote` plus `espressif/esp_hosted` carry each of those
calls to the companion over SDIO, so nothing in the file names the transport.
`platform::NetworkStart()` / `IsNetworkUp()` / `NetworkStatus()` are the seam;
the simulator answers "the machine's own network, already up".

Four things to know:

- **The SSID and passphrase are compiled in** (`WIFI_SSID` / `WIFI_PASSWORD` in
  WifiEsp.cpp), overridable from the build the way the provisioning pair is.
- **It comes up last**, after the model loop, in `app::Startup()`. esp_hosted
  and lwip want internal RAM and tasks of their own, and the three contiguous
  32 kB stacks have to be placed while the heap is still clean. Wi-Fi took the
  image from 1.58 MB to 1.80 MB of flash and static DIRAM to 132 kB; whether
  the stacks still fit is a question only the boot log answers, and
  `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1` is already the fallback that was spent.
- **The C6 has to be carrying ESP-Hosted slave firmware**, which is how it
  ships. `managed_components/espressif__esp_hosted/docs/esp32_p4_function_ev_board.md`
  has both ways to replace it: `esp_hosted_slave_ota(url)` from the running P4,
  or an ESP-Prog on the C6's own UART with the P4 parked in bootloader mode so
  it cannot drive the C6's reset line.
- **The SDIO pins are esp_hosted's defaults** -- CMD 19, CLK 18, D0-D3 14-17,
  slave reset GPIO 54, 4-bit at 40 MHz -- and they are in `sdkconfig`, not in
  any source file. If Wi-Fi never associates, that list is the first thing to
  check against the board.

**Options live in a key/blob store.** `src/StorageOptions.h/.cpp` keeps one
entry per `option::Key`, named `opt_<number>`: Common already packs each option
into a flatbuffer through `Container::GetBLOB()` / `SetBLOB()`, and a key/blob
store is exactly what that wants, so the two meet directly with no framing of
our own. That is deliberately *not* `Container::Save()`, which packs everything
into one image behind a size and a CRC -- the right shape for the raw flash the
other products write to, but here it would mean rewriting every option to
change one.

What the entries land in is `app::BlobStore`, and it is the only part of this
that differs between the builds:

- **the board** uses NVS (`port/esp/BlobStoreNvs.cpp`), which brings its own
  wear levelling and per-entry CRC. `partitions.csv` carries two 4 MB app slots
  (`ota_0`, `ota_1`) plus `otadata`, so the firmware can be replaced over the
  air, and a 24 kB `settings` NVS partition of our own -- separate from the
  default `nvs`, which is IDF's for Wi-Fi and PHY calibration;
- **the simulator** uses a directory of files (`port/pc/BlobStoreFile.cpp`),
  one `<key>.blob` each, under `$ESPP4_SIM_STATE` or `$XDG_STATE_HOME/espp4-sim`.
  Writes go to a temporary file and are renamed into place, which buys what the
  per-entry CRC buys on the board: an interrupted write leaves the previous
  value rather than half of the new one.

`AppOptions.h/.cpp` exists because `option::Container` keeps its item list
protected: `app::Options` derives from `option::ModelBase` so `Settings` can
walk the registered keys and their dirty flags. It also registers
`option::Key::LastKnownCoordinate`, which is what finally makes
`Model::SaveLastKnownCoordinate()` do something.

Three things to know:

- **The `settings` partition is small because of internal RAM, not flash.**
  Mounting an NVS partition costs internal RAM roughly in proportion to its
  size and never returns it. `CONFIG_NVS_ALLOCATE_CACHE_IN_SPIRAM=y` moves the
  page cache and key hash list to PSRAM and is on, but it does not move
  everything: mounting still costs ~12 kB at 24 kB and ~40 kB at 184 kB.
  Without it, 184 kB left the CAN thread unable to get its contiguous 32 kB
  stack and the board aborted at boot with `pthread: Failed to create task`.
  `partitions.csv` carries the measurements; re-read app_main's `largest block`
  line after changing the size.
- **The big stacks are taken first, on purpose.** `app::Startup()` starts the
  serial console before the model loop, and `StartModelLoop()` starts the CAN
  port before mounting the settings store. Three things each need a contiguous 32 kB -- the LVGL
  task, the console task, the CAN thread -- and after boot the largest free
  internal block is about 31 kB. There is no room for a fourth.
- **Only dirty options are written.** `Settings::Save()` walks the per-key
  dirty flags; pass `false` to force the whole set out, which is what populates
  a fresh store on the first boot and what the console's `w` command does.

**The Kaledi item renderer is `src/Item/` as WebAssembly.** `port/wasm/`
compiles the panel items and `PainterTvg` for a browser, so the layout editor
previews the widget the panel will actually draw rather than a second drawing
of it in JavaScript. `kaledi::Renderer` (port/wasm/KalediRenderer.h) is the
whole surface: `setParameter` (one `parameter::fbs::ParamItem`), `setParameters`
(the packed whole-container blob), `loadDefaults`, `setValue`, `setStyle`,
`getParameters`, `render`. Parameters and values persist across calls and can
change during the run; `render` takes one item as JSON and answers a
transparent RGBA pixmap of it.

What made it possible is one seam: **the parameter container is handed to
`item::Panel`'s constructor** rather than reached for. `Panel::FindParameter()`
used to call `app::GetModel()`, which dragged the flight model, the CAN stack
and the NOD behind an object that needs nothing but a parameter, a box and a
painter. `Scene::Build()` now passes `pModel->GetParameters()` -- which is the
other half of why it runs after the model loop -- and the free
`item::MakeItem(parameters, style, cfg)` takes one too, so the wasm module
builds the very same items out of a container of its own. `Scene::m_panel` is a
`std::optional<item::Panel>` for that reason: `g_scene` is a file static and
exists long before the model does.

Seven things to know:

- **The parameters come from Common's own loader, not `app::Parameters`.**
  `parameter::ParameterLoaderBase::CreateNODs()` walks Common's default
  id -> (function, user unit, count, names) table, lays a `ParamStorage` blob
  over it and wires each callback to a `can::DirectNOD` -- the path Nesis takes.
  `app::Parameters` hardcodes the four this firmware shows, which is no use to
  an editor. `CreateCallback()` is the same thing for one id.
- **`ParameterContainer::Find()` never returns nullptr.** An id it does not
  hold gets a default-constructed dummy filed under `can::Id::Invalid`, whose
  value vector is empty -- so an item built on it reads past the end on its
  first frame. The guard is `pP->GetId() == eId`, and it is now in both
  `item::MakeItem()` and the renderer. (`Find()` also *inserts* that dummy, so
  a const container is mutated by a miss; it is node-based, so the `const
  Param*` an item already holds stays good.) Common's default table also *opens* with
  an `Id::Invalid` "Placeholder" row, so `GetCount()` is two more than the
  number of parameters that can be drawn.
- **`SetValue()` forces the value past the filter.** `Parameter::GetValueSystem()`
  low-passes with the function's time constant and re-samples at most every
  30 ms; without `ForceValue()` an editor would show the number creeping
  towards the one that was typed.
- **LVGL's canvas buffer is premultiplied; `ImageData` is not.** White drawn at
  alpha 120 reads `(120,120,120,120)`. The readout divides alpha back out --
  skip it and every antialiased edge composites dark. It also swizzles: LVGL's
  ARGB8888 is B,G,R,A.
- **The headless display gets no buffers at all.** Nothing is ever refreshed --
  an item is drawn into the canvas's own buffer by `init_layer()`/
  `finish_layer()`, which never goes through the display. The obvious gesture,
  one pixel to keep LVGL happy, **hangs**: `lv_display_set_buffers()` sizes rows
  against the display's render format, four bytes a pixel at `LV_COLOR_DEPTH 32`,
  while `sizeof(lv_color_t)` is three.
- **`port/wasm/lv_conf.h` is `port/pc/lv_conf.h` with `LV_USE_SDL 0`**, and
  nothing else. Keep them in step. `LV_USE_OS LV_OS_NONE` and
  `LV_DRAW_SW_DRAW_UNIT_CNT 1` are what keep threads -- and therefore
  SharedArrayBuffer and cross-origin isolation -- out of the editor's server.
- **`port/wasm/test/smoke.mjs` is the only automated test in this repository.**
  It drives the module under Node (33 checks), builds a real `ParamItem` flatbuffer to push
  (`parambuilder.mjs`, against the schema's field order), and checks that
  something was drawn, that something was left clear, that the alpha is
  straight, and that the pushed bands reach the pixels. With an output
  directory it writes a PNG per kind, which is how the part no assertion covers
  gets checked.

### The canvas must stay ARGB8888

`lv_draw_sw_vector` renders straight into an ARGB8888/XRGB8888 buffer. For any
other format — including the panel's native RGB565 — it allocates a temporary
full-area ARGB8888 buffer and blends back **on every frame**. Do not "optimise"
the canvas to RGB565. This is why the simulator's `LV_COLOR_DEPTH 32` and the
board's 16 do not matter here: the canvas is ARGB8888 in both.

## Constraints that are load-bearing

Changing any of these will break the build or the board. `README.md` has the
full reasoning; the short version:

- **LVGL is pinned to `~9.5.0`.** 9.5 renamed `lv_vector_dsc_*` →
  `lv_draw_vector_dsc_*`; `lvgl_cpp` requires `^9.4` and wraps the new names.
- **`CONFIG_LV_USE_OBJ_PROPERTY=y`** — `lvgl_cpp`'s `PropertySetters` mixin uses
  `lv_property_t` unconditionally on 9.5+, and that type only exists with this on.
- **`CONFIG_LV_USE_OBJ_PROPERTY_NAME=y`** — works around an upstream LVGL 9.5.0
  bug: the `!LV_USE_OBJ_PROPERTY_NAME` branch of `lv_obj_class_property_get_id()`
  references a non-existent `obj` and does not compile.
- **`RAPIDJSON_HAS_EXCEPTIONS=0`** in `main/CMakeLists.txt` — ThorVG's Lottie
  parser compiles even with Lottie off, and ESP-IDF builds C++ without exceptions.
- **Stack sizes are not negotiable downward.** ThorVG's `rleRender()` puts a fixed
  16 KB `Cell` buffer on the stack, so any thread that rasterises needs ≥ 32 KB:
  `CONFIG_LV_DRAW_THREAD_STACK_SIZE`, `CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT`
  (ThorVG's own `std::thread` workers, spawned when `LV_DRAW_SW_DRAW_UNIT_CNT > 1`),
  and `cfg.lv_adapter_cfg.task_stack_size` in `port/esp/MainEsp.cpp`. Setting
  `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1` removes the ThorVG worker pool entirely and
  is the fallback if internal RAM runs short.
- **`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`** — the only host link is
  `/dev/ttyACM0`. As a *secondary* console USB-Serial/JTAG is output-only, so the
  debug console cannot be driven from the host unless it is primary.
- **`CONFIG_LV_USE_SNAPSHOT=y`** — needed by the console's screenshot command.
- **`port/pc/lv_conf.h` is LVGL's own template with eleven values changed**, and
  is kept in LVGL's style -- doxygen comments and all -- so that an LVGL upgrade
  is a re-copy and a diff. The kanardia-style skill skips it for that reason.
  Its header lists every change and why. `port/wasm/lv_conf.h` is a copy of it
  with a twelfth (`LV_USE_SDL 0`) and is skipped for the same reason; an
  upgrade is a re-copy of one and a one-line diff to the other.
- **This IDF is v5.5.1.** It does not know ESP32-P4 rev 3.x or 250 MHz PSRAM.
  Waveshare's own examples set `CONFIG_ESP32P4_REV_MIN_300` and
  `CONFIG_SPIRAM_SPEED_250M`, which do not exist here and silently drop PSRAM to
  20 MHz. This project uses `CONFIG_SPIRAM_SPEED_200M` behind
  `CONFIG_IDF_EXPERIMENTAL_FEATURES=y`. Do not copy Waveshare config verbatim.

## Code style

Everything in this section is enforced by the **kanardia-style skill**, which
is how you should apply it -- `.claude/skills/kanardia-style/style.py check`
reports, `format` fixes, `new Foo` scaffolds a banded `Foo.h`/`Foo.cpp` pair.
It reads `SKILL.md` for the reasoning; the short version:

**Naming.** Our own code: `m_` Hungarian members (`m_fPhase`, `m_eMode`,
`m_canvas`) and PascalCase methods (`Build()`, `DrawGauge()`, `CreateScene()`).
Calls into LVGL and `lvgl_cpp` keep those libraries' snake_case names. This is
the one rule the script cannot check for you.

**Every `.h` and `.cpp` in `src/` and `port/` opens with the Kanardia copyright
banner**,
then -- in a header -- `#pragma once`. Never an `#ifndef` include guard. An
`#ifndef` around a *valued* `#define` is a configuration default rather than a
guard and stays put, which is what `CAN_NODE_ID` in `ApplicationDefines.h` is.

**Comments are plain prose in `//` lines, with no doxygen at all** -- no `/**`,
`///`, `///<`, `@file`, `@brief`, `@param`, `@return`, `@p`. A multi-paragraph
file header is a run of `//` lines with a bare `//` between paragraphs. Apart
from the banner, `/* */` survives in exactly one place in the project: the
mid-expression comment in `AppModel.cpp`'s constructor initialiser list, where
`//` would swallow the rest of the line.

**Layout is `clang-format-20` against the repo's own `.clang-format`** -- tabs,
`IndentWidth: 3`, Allman braces for definitions and K&R for control flow,
`SpaceBeforeParens: Never`, `PointerAlignment: Left`, `ColumnLimit: 120`,
`SortIncludes: false` (the include order is organised by hand). Install it once
with `sudo apt install clang-format-20`.

Two traps, both handled by the skill's script and both worth knowing if you
ever run `clang-format-20 -i` yourself: it needs **three or four passes to
converge**, because trailing-comment positions settle late; and converting a
space-indented file to 3-column tabs it leaves **one stray space per tab in
front of every comment line**, which puts the comment a column off from the
code it describes and which it then treats as a fixed point. Leading tabs
followed by *more* spaces than tabs are genuine continuation-line alignment and
must be left alone.

None of this applies outside `src/` and `port/`. `managed_components/`, the
shared `Public/Common` tree and the two `lv_conf.h` (`port/pc/`, `port/wasm/`)
are third-party as far as this repo is concerned, carry their own style, and
are never reformatted. `port/wasm/demo/` and `port/wasm/test/` are JavaScript
and outside the skill's reach either way.

## Known board quirks

- `W ledc: GPIO 26 is not usable` at boot means backlight PWM init failed, so
  `bsp_display_backlight_on()` is a no-op. If the panel is dark, start there.
- BSP 3.0.1 leaves GT911 RST/INT unconfigured, probes `0x5D` then `0x14`, and
  polls without an ISR. `cfg.touch_flags` in `port/esp/MainEsp.cpp` flips axes.
- `on_frame_buf_complete unavailable` means `TEAR_AVOID_MODE_TRIPLE_PARTIAL` may
  not fully suppress tearing; try `DOUBLE_FULL`.
