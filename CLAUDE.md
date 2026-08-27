# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A single-app ESP-IDF project: an LVGL 9 UI with ThorVG vector graphics, in C++,
for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-4C** (4", 720×720 round IPS,
2-lane MIPI-DSI / JD9365, GT911 touch). There is no test suite and no linter —
the compiler and the board are the feedback loop.

## Commands

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
(`main/SerialConsole.cpp`): `i` stats (scene, frame time, heap, and the model's
rpm/eng/moving/stack), `t` toggle scene, `s`/`S` screenshot as
base64 RGB888. `.claude/skills/run-espp4/` documents the protocol and ships
`driver.py`, which is how you smoke-test the board or get a PNG of the panel
without looking at it. Read that SKILL.md before touching serial or snapshots --
it lists the traps (port-open resets the chip, logs corrupt the base64 stream,
LVGL's RGB888 is B,G,R).

Measured on hardware: **~100-170 ms per ThorVG frame**, so the UI runs at 6-8 fps
against a 33 ms timer, and **internal heap dips to ~34 KB** while the gauge
renders. That heap figure is the tightest resource in the project.

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

**Common → the flight model.** `AppModel.h/.cpp` derive a concrete
`avio::ModelBase` and tick it on its own FreeRTOS task: `Update50ms()` on a
50 ms beat, `Update1s()` once per second. There is no CAN bus, GNSS receiver or
options storage on this board, so the abstract hooks answer "nothing connected"
and `Model::Simulate()` plays the part of the engine ECU by writing
`can::Id::EngineRPM_1` into the `DirectNOD`. Everything above the NOD -- GNSS,
navigation, clock, sunrise/sunset, the above/below detectors behind
`IsFlying()` / `IsEngineRunning()` / `IsMoving()` -- is the unmodified shared
code. The scale scene reads its rpm from the model, and the console's `i` line
carries `rpm=`, `eng=`, `moving=` and `model_stack=` so the loop can be checked
from the host.

`VectorScene.cpp` holds one file-static `Scene`. Its widgets are
`std::optional<T>` members constructed in `Build()`, because binding objects are
move-only and take ownership of the underlying `lv_obj_t`. A `lvgl::Timer`
re-renders the canvas at ~30 fps.

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
