# ESP32-P4 · LVGL 9 + ThorVG demo

A C++ ESP-IDF project for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-4C**
(4", 720×720 round IPS, 2-lane MIPI-DSI / JD9365, GT911 capacitive touch)
that renders **vector graphics with ThorVG** inside an **LVGL 9** UI, written
through the **pedapudi/lvgl_cpp** C++ binding.

Tap the screen to cycle the scenes:

| Scene       | What it shows                                                            |
|-------------|--------------------------------------------------------------------------|
| `gauge`     | radial-gradient disc, gradient-stroked arc, 61 tick marks, a bezier needle |
| `scale`     | engine tachometer -- a Kanardia `scale::Style` drawn by `Scale::DrawArc()` |
| `ias`       | airspeed indicator -- `Scale::DrawArcIAS()`: coloured arcs, white flap band, Vne radial, V-speed marks |
| `altimeter` | three-pointer altimeter, full-circle scale, hundreds / thousands / ten-thousands hands |
| `rpm`       | engine and rotor tachometers side by side, scales mirrored `)(`, a marker riding each |
| `menu`      | the settings page: a menu of levels under an eye-shaped title bar cut to the round glass |

The four instrument scenes are drawn from the shared Kanardia `Public/Common`
code and read their values from a `parameter::ParameterContainer`. A live `ms/frame` readout
shows what one ThorVG frame actually costs.

The settings page ([src/MenuPage.h](src/MenuPage.h)) is a screen of its own and
a view onto `app::Options` -- the same option container every Kanardia product
keeps. A row asks the option what it holds and a tap hands the next value
back; Common already knows which units a group allows, so the level table is
close to one line per row. The frame timer stops while it is up.

It is also navigable by key -- up/down to move, Enter to activate, Esc to go
back -- which works out of the box against the simulator's SDL keyboard and
would work against a keypad or a rotary knob on the board. Changes are written
once, on the way out.

It is built twice from one set of sources: as firmware for the board, and as a
**desktop simulator** that puts the same UI in an SDL window. See
[Two builds](#two-builds).

**Status:** running on hardware. Boot, panel, touch, all five scenes, the model
loop and screenshot capture verified on a rev v1.3 board over `/dev/ttyACM0`.
The simulator runs the same scenes, model, CAN stack and console on a desktop.

---

## How LVGL, ThorVG and lvgl_cpp fit together

LVGL 9 vendors ThorVG in-tree (`src/libs/thorvg`) and exposes it as the
`lv_draw_vector_*` C API. You do not add ThorVG as a separate component — you
switch it on in LVGL's Kconfig:

```
CONFIG_LV_USE_VECTOR_GRAPHIC=y   # the vector drawing API
CONFIG_LV_USE_THORVG=y           # rasteriser
CONFIG_LV_USE_THORVG_INTERNAL=y  # use the bundled copy
```

`lvgl_cpp` then wraps that API as `lvgl::VectorDraw` / `lvgl::VectorPath`, and
the widgets as `lvgl::Canvas`, `lvgl::Label`, `lvgl::Timer` with a fluent,
RAII interface. All of it is already in [`sdkconfig.defaults`](sdkconfig.defaults).

The demo draws into an **ARGB8888 `lvgl::Canvas`**. That format matters:
`lv_draw_sw_vector` renders straight into an ARGB8888/XRGB8888 buffer, but for
any other format (such as the panel's RGB565) it allocates a temporary
full-area ARGB8888 buffer and blends back — every frame.

## Two builds

`src/` is the product and names no operating system. Everything that only a
machine can answer goes through `src/Platform.h` -- logging, time, the LVGL
lock, threads, the host link, heap figures -- plus three interfaces with a
lifetime of their own: `app::CanPort`, `app::BlobStore` and
`platform::FirmwareTarget`.

| | board (`port/esp/`) | simulator (`port/pc/`) |
|---|---|---|
| display | Waveshare BSP, MIPI-DSI panel | LVGL's SDL driver, a 720×720 window |
| input | GT911 touch | the mouse |
| CAN | the P4's TWAI controller | `can::CanuCan` on a Kanardia CANU adapter |
| no bus | self-test: RX mapped onto TX, NO_ACK | self-test: a queue and the receive thread |
| settings | one NVS entry per key, `settings` partition | one file per key under `$XDG_STATE_HOME/espp4-sim` |
| firmware update | ESP-IDF OTA into the spare app slot | a file next to the settings |
| console | USB-Serial/JTAG | stdin/stdout |
| threads | FreeRTOS tasks, sized stacks | `std::thread` |
| build | `idf.py build` (`main/CMakeLists.txt`) | `cmake -S port/pc -B build-sim` |

The simulator is not a mock: the scenes, the ThorVG rasteriser, the
`avio::ModelBase` flight model, the CANaerospace stack and the debug console are
the same code. What it cannot tell you is anything about the panel, the touch
controller, timing or memory -- it reports its heap figures as zero, and a gauge
frame that costs ~60 ms on the board costs ~1.5 ms on a desktop.

```bash
sudo apt install libsdl2-dev
cmake -S port/pc -B build-sim -G Ninja
cmake --build build-sim
./build-sim/espp4-sim --can /dev/ttyUSB0        # both switches optional
```

LVGL and `lvgl_cpp` come out of `managed_components/` when it is there -- the
very sources the board builds -- and are fetched at the pinned versions when it
is not.

## Code style

Our own code uses `m_` Hungarian members (`m_fPhase`, `m_canvas`) and
PascalCase methods (`Build()`, `DrawGauge()`). Calls into LVGL and lvgl_cpp keep
those libraries' own snake_case names.

---

## Requirements

* ESP-IDF **v5.5 or newer** (the BSP requires `idf: >=5.5`). Built and verified
  against the v5.5.1 checkout in `~/esp/v5.5.1/esp-idf`.
* One-time toolchain install if you have not built for ESP32-P4 before:
  ```bash
  ~/esp/v5.5.1/esp-idf/install.sh esp32p4
  ```

Dependencies are resolved automatically:

| Component | Source | Role |
|---|---|---|
| `waveshare/esp32_p4_wifi6_touch_lcd_xc` | registry | board support: MIPI-DSI panel, GT911 touch, backlight |
| `lvgl/lvgl` `~9.5.0` | registry | GUI + bundled ThorVG |
| `lvgl_cpp` | git (`main`) | C++ binding for LVGL |
| `espressif/esp_lvgl_adapter` | pulled in by the BSP | LVGL task, tear avoidance, PPA |

## Build and flash

```bash
. ~/esp/v5.5.1/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Driving it from a host

The firmware has a one-character debug console on USB-Serial/JTAG
([src/SerialConsole.cpp](src/SerialConsole.cpp)) so the board can be smoke-tested
and screenshotted without looking at the panel:

```bash
python3 .claude/skills/run-espp4/driver.py smoke
python3 .claude/skills/run-espp4/driver.py shot --scene altimeter --full --out alt.png
```

`shot` renders the whole 720x720 screen with `lv_snapshot_take()`, streams it out
as base64 RGB888 and writes a PNG (3.7 s at full resolution, 1.8 s at half).
See [.claude/skills/run-espp4/SKILL.md](.claude/skills/run-espp4/SKILL.md) for the
protocol and the traps. Build with `-DDEMO_NO_SERIAL_CONSOLE` to compile the
console out.

## Measured on hardware

| | |
|---|---|
| ThorVG frame time | **gauge ~60 ms, rpm ~58 ms, ias ~38 ms, scale ~34 ms, altimeter ~28 ms** |
| Effective UI rate | ~16 fps on the gauge; the instrument scenes reach the 33 ms timer |
| Free internal heap | low-water mark **~32 kB**; largest contiguous block **~31 kB** |
| NVS store | 4 option blobs + a 451 B parameter blob, 34 of 756 entries |
| Parameter push | one parameter as **368 B** over 92 CAN messages in ~600 ms, CRC-16 checked |
| Free PSRAM | ~28.9 MB |

The frame time is software rasterisation of a 400x400 ARGB8888 canvas on a
360 MHz core. If you need it faster, shrink `CANVAS_SIZE` in
[src/VectorScene.cpp](src/VectorScene.cpp) -- cost scales with area.

---

## Things worth knowing before you change anything

**Internal RAM is what the partition table is really trading against.**
Mounting an NVS partition costs internal RAM roughly in proportion to its size
and never gives it back, and three things on this board each need a *contiguous*
32 kB of it: the LVGL task, the console task and the CAN thread.
`CONFIG_NVS_ALLOCATE_CACHE_IN_SPIRAM=y` pushes NVS's page cache and key hash
list into PSRAM, but only part of the cost follows: mounting still takes ~12 kB
at 24 kB of partition and ~40 kB at 184 kB. Without that option a 184 kB
`settings` partition left too little and the board aborted at boot with
`pthread: Failed to create task`. `app::Startup()` also starts the console before the
model loop so the big stacks are taken while the heap is still clean.
[partitions.csv](partitions.csv) carries the measurements.

**LVGL is pinned to `~9.5.0`, and that is load-bearing.** LVGL 9.5 renamed the
vector API (`lv_vector_dsc_*` → `lv_draw_vector_dsc_*`); `lvgl_cpp` requires
`^9.4` and wraps the new names. Going back to 9.3 breaks the binding.

**Two LVGL options exist only to keep `lvgl_cpp` compiling:**

* `CONFIG_LV_USE_OBJ_PROPERTY=y` — the binding's `PropertySetters` mixin uses
  `lv_property_t` / `lv_obj_set_property` unconditionally on LVGL 9.5+, and those
  types only exist with this option on.
* `CONFIG_LV_USE_OBJ_PROPERTY_NAME=y` — works around an upstream bug in LVGL
  9.5.0: `lv_obj_class_property_get_id()` has `LV_UNUSED(obj)` in its
  `!LV_USE_OBJ_PROPERTY_NAME` branch where no `obj` exists, so that branch does
  not compile. Turning names on takes the other path, at the cost of a const
  name table per widget class.

**Chip revision and PSRAM speed depend on your IDF version.** Waveshare's own
examples pin ESP32-P4 rev 3.x and 250 MHz PSRAM, but **ESP-IDF 5.5.1 only knows
revisions up to v1.0 and PSRAM up to 200 MHz** — `CONFIG_ESP32P4_REV_MIN_300`
and `CONFIG_SPIRAM_SPEED_250M` do not exist there and are *silently ignored*,
which drops PSRAM to its 20 MHz default. This project therefore uses
`CONFIG_SPIRAM_SPEED_200M=y` (gated behind `CONFIG_IDF_EXPERIMENTAL_FEATURES=y`)
and leaves the minimum revision at the IDF default. On IDF ≥ 5.5.5 you can
switch to the rev-3.x / 250 MHz settings. Always check the generated `sdkconfig`
after editing `sdkconfig.defaults` — unknown symbols fail quietly.

**Stack sizes.** ThorVG is far hungrier than LVGL's own draw code:

* `CONFIG_LV_DRAW_THREAD_STACK_SIZE=32768` — LVGL's own note says 32 kB+ when ThorVG is on.
* `CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT=16384` — with `LV_DRAW_SW_DRAW_UNIT_CNT > 1`,
  ThorVG compiles in its own `std::thread` worker pool; the 3 kB pthread default overflows.
* `cfg.lv_adapter_cfg.task_stack_size = 32 kB` in `port/esp/MainEsp.cpp` — the canvas is
  rasterised from the LVGL task.

If you hit a stack overflow anyway, set `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1`: that
makes LVGL init ThorVG with zero worker threads and render on the calling thread.

**RapidJSON.** LVGL's ThorVG copy compiles its Lottie parser (and therefore
RapidJSON) whenever `LV_USE_THORVG_INTERNAL` is on, even with Lottie disabled.
ESP-IDF builds C++ without exceptions, so `main/CMakeLists.txt` defines
`RAPIDJSON_HAS_EXCEPTIONS=0` on the LVGL component.

**Lottie.** To play `.json` Lottie animations, set `CONFIG_LV_USE_LOTTIE=y` — that
turns on ThorVG's SVG and Lottie loaders and grows the binary noticeably.
`lvgl_cpp` has an `lvgl::Lottie` widget behind the same flag.

**Display type.** The BSP covers both boards in the XC family. This project sets
`CONFIG_BSP_LCD_TYPE_720_720_4_INCH=y` (the 4C). For the 3.4C use
`CONFIG_BSP_LCD_TYPE_800_800_3_4_INCH=y` instead.

**Touch.** Waveshare's own notes on BSP 3.0.1 say touch RST/INT are left
unconfigured, the driver probes `0x5D` then `0x14`, and it polls rather than using
an ISR. If the tap-to-switch does not respond, that is the first thing to check;
`cfg.touch_flags` in `port/esp/MainEsp.cpp` is where you would flip axes.

**One escape hatch to the C API.** `lvgl_cpp`'s `set_stroke_dash()` ignores an
empty vector, so it cannot clear a dash pattern. `DrawGauge()` calls
`lv_draw_vector_dsc_set_stroke_dash(dsc.raw(), nullptr, 0)` directly — `raw()`
is the binding's supported way down to the C layer.

---

## Layout

```
.claude/skills/run-espp4/  run skill: SKILL.md + driver.py (board and --sim)
CMakeLists.txt           ESP-IDF project definition
partitions.csv           2 x 4 MB OTA app slots + 24 kB settings NVS, on 16 MB flash
sdkconfig.defaults       target, PSRAM, LVGL, ThorVG and lvgl_cpp configuration
cmake/
  KanardiaSources.cmake  the source lists both builds compile
  KanardiaFonts.cmake    lv_font_conv: Kanardia 14/16/20/28 + KanardiaFont.h
src/                     the product -- no operating system named anywhere in here
  App.h/.cpp             the boot both builds share, once a display exists
  Platform.h             what the application asks of the machine underneath it
  CanPort.h              the CAN port as the application sees it
  BlobStore.h            where a packed option or parameter blob is kept
  AppModel.h/.cpp        concrete avio::ModelBase + its 50 ms processing task
  VectorScene.h/.cpp     the LVGL UI and all ThorVG drawing
  Painter.h              the drawing back end the scale is written against
  PainterTvg.h/.cpp      that concept on ThorVG/LVGL; PainterQt.h is the Qt twin
  ScaleDraw.h/.cpp       the Kanardia scale, drawn once, against either back end
  MenuPage.h/.cpp        the settings page: menu levels over an eye-shaped title bar
  CanProcessor.h/.cpp    CANaerospace decode: NOD, units, pushed-parameter receive
  ApplicationDefines.h   our CAN node id and the services we implement
  AppOptions.h/.cpp      the option set we keep, and the keys Settings walks
  AppParameters.h/.cpp   parameter::ParameterContainer, fed from the NOD
  StorageOptions.h/.cpp  the options and the parameter blob, over a BlobStore
  SerialConsole.h/.cpp   debug console: stats, scene and menu toggle, save settings, screenshot
  KanardiaCommon.h       Qt shim so Public/Common compiles for both targets
port/esp/                the board
  MainEsp.cpp            starts the BSP display, hands over to app::Startup()
  PlatformEsp.cpp        ESP-IDF and the BSP behind Platform.h
  CanPortEsp.h/.cpp      app::CanPort on the P4's TWAI controller
  BlobStoreNvs.cpp       app::BlobStore on the `settings` NVS partition
  FirmwareEsp.cpp        platform::FirmwareTarget on ESP-IDF's OTA API
port/pc/                 the desktop simulator
  CMakeLists.txt         the simulator's own build; needs no ESP-IDF
  lv_conf.h              LVGL's template with eleven values changed
  MainSim.cpp            opens the SDL window, then app::Startup(), then LVGL's loop
  PlatformSim.cpp        POSIX and the standard library behind Platform.h
  CanPortCanu.h/.cpp     app::CanPort on can::CanuCan, with a software loopback
  BlobStoreFile.cpp      app::BlobStore on a directory of files
  FirmwarePc.cpp         platform::FirmwareTarget on a file
main/
  CMakeLists.txt         the ESP-IDF component: src/ + port/esp/ + Common + fonts
  idf_component.yml      BSP + LVGL + lvgl_cpp dependencies
```

## References

* [Waveshare ESP32-P4-WIFI6-Touch-LCD-4C wiki](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4C)
* [waveshareteam/ESP32-P4-WIFI6-Touch-LCD-XC](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-XC) — vendor examples
* [waveshareteam/Waveshare-ESP32-components](https://github.com/waveshareteam/Waveshare-ESP32-components) — the BSP source
* [pedapudi/lvgl_cpp](https://github.com/pedapudi/lvgl_cpp) — the C++ binding
* [LVGL vector graphics docs](https://docs.lvgl.io/master/details/main-modules/draw/vector.html)
