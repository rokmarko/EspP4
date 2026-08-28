# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A single-app ESP-IDF project: an LVGL 9 UI with ThorVG vector graphics, in C++,
for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-4C** (4", 720×720 round IPS,
2-lane MIPI-DSI / JD9365, GT911 touch). There is no test suite and no linter —
the compiler and the board are the feedback loop.

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

## Driving the board

The firmware carries a one-character debug console on USB-Serial/JTAG
(`main/SerialConsole.cpp`): `i` stats (scene, frame time, heap, the model's
rpm/eng/moving/stack, the CAN counters, the NVS entry count and the internal
heap low-water mark), `t` toggle scene, `w` write the option blobs to NVS,
`P` push a parameter at ourselves over CAN, `s`/`S` screenshot as base64
RGB888. `.claude/skills/run-espp4/` documents the protocol and ships
`driver.py`, which is how you smoke-test the board or get a PNG of the panel
without looking at it. Read that SKILL.md before touching serial or snapshots --
it lists the traps (port-open resets the chip, logs corrupt the base64 stream,
LVGL's RGB888 is B,G,R).

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

## Architecture

Three layers, each of which has bitten this project at least once:

**BSP → LVGL.** `Main.cpp` does nothing but build a `bsp_display_cfg_t` and call
`bsp_display_start_with_config()`. The Waveshare BSP brings up the DSI panel,
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

**Kanardia Common → the scale.** `main/CMakeLists.txt` compiles a hand-picked
subset of the shared `Public/Common` tree (parameter bands, units, scale utils)
straight out of the working copy at `$ENV{HOME}/Branch/v4_3`, exactly the way
`Private/Horis/v1/CMakeLists.txt` does. Override the location with
`idf.py -DKANARDIA_BRANCH=/path/to/v4_3 build`; the build fails loudly if the
tree is missing. `ScaleDrawTvg.cpp` is the ThorVG twin of
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
  will not compile. `KanardiaCommon.h` adds a constrained mixed-type overload,
  and `main/CMakeLists.txt` force-includes it into every Common source with
  `-include`. The same mismatch turns Common's `%u` formats into
  `-Werror=format` failures, hence `-Wno-format` on those sources. Upstream both
  want fixing properly (`common::IsInside<int32_t>(...)`, `PRIu32`).
- **Two Common sources need exceptions back.** `BLOB/BLOBPackUnpack.cpp` and
  `Compress/CompressZeros.cpp` `throw` on malformed input, which will not
  compile under IDF's global `-fno-exceptions`. They are built with
  `-fexceptions`; the throwing branches are unreachable here, and would
  terminate if they ever fired. `-DNO_LZO_COMPRESSION` (Common's own switch)
  keeps miniLZO out of the image.

**Fonts are generated, not checked in.** `main/CMakeLists.txt` runs
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
`Main.cpp` installs with `avio::format::SetUnitFormatter()` before the scene is
built. Everything below reaches it from there -- `avio::format::ToString()`,
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
tree, so it just selects the logging branch Common already intends.

**Common → the CAN bus.** `CanPortEsp` implements `can::AbstractCanPort` on
the P4's TWAI controller, and `CanProcessor` is the CANaerospace side: it
mirrors `can::CANHandler::Process()` -- split incoming messages by id range into
services, status, NOD and special -- without the `CANHandler` template, which
needs a product's whole sender/service stack. NOD messages go into the
`DirectNOD` the model reads; sign-of-life messages go into
`can::uCUnitInfoContainer`, the microcontroller-side unit container.

`main/ApplicationDefines.h` is the extension point Common expects from every
product: our node id and which halves of the optional services we implement.
`USE_CAN_MIS_A` asks other modules to identify themselves and answers nothing,
so the container's "identified" count stays at zero on a bus where nobody
answers -- correct, not a fault. `USE_CAN_DDS_B` and `USE_CAN_MCS_B` are the
receive half of the parameter push described below.

**There is no CAN transceiver on this board**, so the TWAI pins go nowhere.
`Mode::SelfTest` is the default and exercises the whole path anyway: RX is
mapped onto the TX pin so the GPIO matrix loops the signal back, the controller
runs in `TWAI_MODE_NO_ACK` (nobody is there to acknowledge), and frames are
transmitted as self-reception requests (`twai_message_t::self`). All three are
needed -- NO_ACK alone transmits but never receives. In that mode `Simulate()`
and `SendSignOfLife()` put real CANaerospace frames on the controller and they
arrive back through the full decode path. Both go silent in `Mode::Normal`:
on a real bus those ids belong to somebody else.

**Common → the flight model.** `AppModel.h/.cpp` derive a concrete
`avio::ModelBase` and tick it on its own FreeRTOS task: `Update50ms()` on a
50 ms beat, `Update1s()` once per second, plus the CAN processor's one-second
work. There is no GNSS receiver or options storage on this board, so those
hooks answer "nothing connected"; the NOD is fed from the CAN port. Everything above the NOD -- GNSS,
navigation, clock, sunrise/sunset, the above/below detectors behind
`IsFlying()` / `IsEngineRunning()` / `IsMoving()` -- is the unmodified shared
code. The three instrument scenes read rpm, IAS and altitude from the model, and the
console's `i` line carries `rpm=`, `eng=`, `moving=` and `model_stack=` so the
loop can be checked from the host. `SaveLastKnownCoordinate()` is no longer a
stub: it marks the option dirty and lets `Settings::Save()` write it to NVS.

Scenes cycle `gauge` -> `scale` (tachometer, `Scale::DrawArc`) -> `ias`
(airspeed, `Scale::DrawArcIAS`) -> `altimeter` (three pointers over a
full-circle `DrawArc`) -> `rpm` (engine and rotor side by side).
`Arc2D::IsCircle()` is what makes the altimeter drop the label that would
otherwise land on top of its zero.

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

**Labels are always white**, whatever the pen -- `DrawTextAsPath()` in the Qt
original forces white too, and without it `DrawArcIAS` leaves a red pen behind
after the Vne dash and every label comes out red.

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
own node id. In self-test the controller hands every frame back, so the
console's `P` command drives the whole loop for real -- DDS_A, DDS_B, MCS_A,
MCS_B, the apply and the NVS write. The tachometer's bands visibly change, and
survive a reboot.

**The parameter set is saved as one blob.** `Settings::SaveParameters()` /
`LoadParameters()` wrap `parameter::ParamStorage`, which packs the whole
container into a single flatbuffer and LZO-compresses it: 451 bytes for our
three parameters. That is one NVS entry, not one per key -- unlike the options
-- because that packed form is what the rest of the Kanardia tooling reads and
writes, and splitting it would make the blob non-portable. `ParamStorage::Load()`
answers silently on a bad CRC, so `LoadParameters()` proves the blob names at
least one parameter we hold before applying it.

Bringing `ParamStorage` in pulled miniLZO into the image (`LZO/minilzo.c`, built
as C and deliberately outside `SRC_COMMON_FILES`, because those get
`-include KanardiaCommon.h`), plus `Param.cpp`, `ParamContainer.cpp`,
`ParamFuelLevel.cpp`, `CanIdDetails.cpp` and `CRC-32.cpp`.

**Options live in NVS.** `partitions.csv` carries two 4 MB app slots (`ota_0`,
`ota_1`) plus `otadata`, so the firmware can be replaced over the air, and a
24 kB `settings` NVS partition of our own -- separate from the default `nvs`,
which is IDF's for Wi-Fi and PHY calibration.

`StorageOptions.h/.cpp` keeps one NVS entry per `option::Key`, named
`opt_<number>`:
Common already packs each option into a flatbuffer through
`Container::GetBLOB()` / `SetBLOB()`, and NVS is a key/blob store with its own
wear levelling and per-entry CRC, so the two meet directly with no framing of
our own. That is deliberately *not* `Container::Save()`, which packs everything
into one image behind a size and a CRC -- the right shape for the raw flash the
other products write to, but here it would mean rewriting every option to
change one.

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
- **The big stacks are taken first, on purpose.** `Main.cpp` starts the serial
  console before the model loop, and `StartModelLoop()` starts the CAN port
  before mounting NVS. Three things each need a contiguous 32 kB -- the LVGL
  task, the console task, the CAN thread -- and after boot the largest free
  internal block is about 31 kB. There is no room for a fourth.
- **Only dirty options are written.** `Settings::Save()` walks the per-key
  dirty flags; pass `false` to force the whole set out, which is what populates
  a fresh partition on the first boot and what the console's `w` command does.

### The canvas must stay ARGB8888

`lv_draw_sw_vector` renders straight into an ARGB8888/XRGB8888 buffer. For any
other format — including the panel's native RGB565 — it allocates a temporary
full-area ARGB8888 buffer and blends back **on every frame**. Do not "optimise"
the canvas to RGB565.

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
  and `cfg.lv_adapter_cfg.task_stack_size` in `Main.cpp`. Setting
  `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1` removes the ThorVG worker pool entirely and
  is the fallback if internal RAM runs short.
- **`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`** — the only host link is
  `/dev/ttyACM0`. As a *secondary* console USB-Serial/JTAG is output-only, so the
  debug console cannot be driven from the host unless it is primary.
- **`CONFIG_LV_USE_SNAPSHOT=y`** — needed by the console's screenshot command.
- **This IDF is v5.5.1.** It does not know ESP32-P4 rev 3.x or 250 MHz PSRAM.
  Waveshare's own examples set `CONFIG_ESP32P4_REV_MIN_300` and
  `CONFIG_SPIRAM_SPEED_250M`, which do not exist here and silently drop PSRAM to
  20 MHz. This project uses `CONFIG_SPIRAM_SPEED_200M` behind
  `CONFIG_IDF_EXPERIMENTAL_FEATURES=y`. Do not copy Waveshare config verbatim.

## Code style

Our own code: `m_` Hungarian members (`m_fPhase`, `m_eMode`, `m_canvas`) and
PascalCase methods (`Build()`, `DrawGauge()`, `CreateScene()`). Calls into LVGL
and `lvgl_cpp` keep those libraries' snake_case names.

## Known board quirks

- `W ledc: GPIO 26 is not usable` at boot means backlight PWM init failed, so
  `bsp_display_backlight_on()` is a no-op. If the panel is dark, start there.
- BSP 3.0.1 leaves GT911 RST/INT unconfigured, probes `0x5D` then `0x14`, and
  polls without an ISR. `cfg.touch_flags` in `Main.cpp` flips axes.
- `on_frame_buf_complete unavailable` means `TEAR_AVOID_MODE_TRIPLE_PARTIAL` may
  not fully suppress tearing; try `DOUBLE_FULL`.
