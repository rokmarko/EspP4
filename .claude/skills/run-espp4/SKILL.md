---
name: run-espp4
description: Build, flash, run, drive and screenshot the ESP32-P4 LVGL + ThorVG demo firmware on the Waveshare ESP32-P4-WIFI6-Touch-LCD-4C board. Use when asked to run, start, build, flash, smoke-test, screenshot, or check the display/UI of this project.
---

# Run the ESP32-P4 LVGL + ThorVG demo

Firmware for a Waveshare ESP32-P4-WIFI6-Touch-LCD-4C (720×720 round MIPI-DSI
panel). It cannot run in this container — it runs on the board, over
`/dev/ttyACM0`.

**Drive it with `.claude/skills/run-espp4/driver.py`.** The firmware carries a
one-character debug console on USB-Serial/JTAG
([main/SerialConsole.cpp](../../../main/SerialConsole.cpp)); the driver speaks
that protocol, including pulling a real screenshot off the panel and decoding it
to PNG. All paths below are relative to the project root.

## Prerequisites

ESP-IDF v5.5.1 at `~/esp/v5.5.1/esp-idf`, with the ESP32-P4 toolchain installed:

```bash
~/esp/v5.5.1/esp-idf/install.sh esp32p4
```

The **build** additionally needs `lv_font_conv` (Node), because the Kanardia
fonts are generated from the TTF at build time. Install it once:

```bash
npm install --prefix tools lv_font_conv
```

CMake looks for it in `tools/node_modules/.bin`, then on `PATH`, and fails with
that instruction if it finds neither.

The driver needs `pyserial`, which lives in the IDF virtualenv — it re-execs
itself there automatically, so serial commands work from a plain `python3` with
no `source export.sh`.

## Run (agent path)

Every subcommand below was run against real hardware.

```bash
python3 .claude/skills/run-espp4/driver.py build          # sources export.sh for you
python3 .claude/skills/run-espp4/driver.py flash
python3 .claude/skills/run-espp4/driver.py smoke          # exit 0 / 1
python3 .claude/skills/run-espp4/driver.py stats
python3 .claude/skills/run-espp4/driver.py monitor --secs 10
python3 .claude/skills/run-espp4/driver.py shot --scene scale   --out scale.png
python3 .claude/skills/run-espp4/driver.py shot --scene altimeter --full --out alt.png
```

`smoke` resets the board, checks five boot markers, fails on any panic marker,
then proves the UI is live by reading stats:

```
[ok] app_main reached         (panel 720x720)
[ok] MIPI-DSI panel driver    (jd9365)
[ok] GT911 touch              (Touch 0x5d found)
[ok] scene built              (canvas 400x400 ARGB8888 ready)
[ok] debug console            (<<<CONSOLE ready>>>)
[ok] stats: <<<STATS scene=gauge frame_ms=166.2 heap_int=132691 heap_psram=28887616 rpm=1650 eng=1 moving=0 model_stack=5120>>>
[ok] ThorVG frame time 166.2 ms

SMOKE PASSED
```

The `rpm`/`eng`/`moving`/`model_stack` fields come from the shared Kanardia
flight model (`main/AppModel.cpp`), and are how you check that its 50 ms
processing loop is actually ticking. `model_stack` is the model task's smallest
free stack in bytes -- internal RAM is the scarce resource here, so watch it.

The `can_*` fields cover the CAN side. The board has no transceiver, so the port
runs in self-test (`can=self-test`): frames are looped back inside the
controller, which still exercises decode, NOD store and the unit container.
`can_rx` should track `can_tx`; `can_nod` is a little lower because the
sign-of-life and module-info frames are services, not NOD. `can_alive` counts
units heard from, `can_ident` counts those that answered the module-information
request -- the latter stays 0 here, because this board implements only the
asking half of that service.

`shot` writes a real PNG of the panel and reports which scene it captured:

```
panel.png  360x360  scene=ias      1.8s     # default (step 2)
panel.png  720x720  scene=gauge    3.7s     # --full
```

### Console protocol

Single bytes, no newline. Useful if you talk to the port directly:

| byte | effect |
|---|---|
| `h` | `<<<HELP ...>>>` |
| `i` | `<<<STATS scene=… frame_ms=… heap_int=… heap_psram=… rpm=… eng=… moving=… model_stack=…>>>` |
| `t` | toggle scene, as a screen tap would |
| `s` | screenshot, every 2nd pixel (360×360, ~1.8 s) |
| `S` | screenshot, full 720×720 (~3.7 s) |

Screenshots are framed as `<<<SHOT step=N fmt=rgb888>>>`, base64 body,
`<<<ENDSHOT ok=1 w=W h=H>>>`.

## Run (human path)

```bash
. ~/esp/v5.5.1/esp-idf/export.sh
idf.py -p /dev/ttyACM0 flash monitor      # Ctrl-] to exit
```

Only useful if you can physically see the panel — the UI emits nothing to
serial on its own. `idf.py monitor` holds the port, so the driver cannot run at
the same time.

## Gotchas

Every one of these cost real debugging time.

- **Opening the port resets the chip.** USB-Serial/JTAG uses DTR/RTS for reset,
  and pyserial asserts them on open. So *no board state survives between driver
  invocations* — `toggle` in one command then `shot` in the next captures the
  default scene. Use `shot --scene ias`, which toggles and verifies inside
  one session. The scenes cycle gauge -> scale -> ias -> altimeter.
- **Stray bytes can toggle the scene behind your back** right after connect
  (leftovers in the USB endpoint). Never assume blind toggling worked; the
  driver reads the scene back from `<<<STATS>>>` and `shot` prints what it
  actually captured.
- **The console task needs a 32 KB stack.** `lv_snapshot_take()` performs draw
  work on the *calling* thread. At the FreeRTOS default of 4 KB the board reset
  mid-screenshot **with no panic text at all** — just a fresh boot log, which
  looks like a USB glitch rather than a stack overflow.
- **Logging must be silenced during a capture.** `ESP_LOG` output goes to the
  same USB endpoint as the base64 body; a log line lands *mid-base64-line* and
  the host cannot strip it (filtering non-base64 characters absorbs the log
  text's letters into the image data, producing "Incorrect padding" or silent
  corruption). `Screenshot()` calls `esp_log_level_set("*", ESP_LOG_NONE)`
  around the capture.
- **LVGL's RGB888 is B,G,R in memory.** `lv_color_t` is
  `{blue, green, red}`, so a snapshot fed straight into a PNG renders the demo's
  blue theme as brown. `driver.py:bgr_to_rgb()` does the swap.
- **USB-Serial/JTAG as a *secondary* console is output-only.** The console could
  not be driven from the host until `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` made
  it primary.
- **`rst:0x` is not a panic marker** — the ROM prints `rst:0x1 (POWERON)` on
  every healthy boot. Match on `Guru Meditation`, `panic'ed`,
  `Stack protection fault`, `Backtrace:`, `WDT_RST` instead.
- **pyserial is only in the IDF venv, and that venv's `bin/python` is a symlink
  to the system python** — deduplicating interpreters by `realpath` makes the
  re-exec silently refuse to run.
- **PIL is in the system python but not the IDF venv.** The driver writes PNGs
  with `zlib` + `struct` so it needs neither.

## Performance and headroom (measured, not estimated)

- **Per ThorVG frame: gauge ~60 ms, ias ~38 ms, scale ~34 ms, altimeter ~28 ms.**
  The animation timer asks for 33 ms, so the gauge runs at ~16 fps and the
  instrument scenes roughly reach the timer. This is software rasterisation of a
  400×400 ARGB8888 canvas on a 360 MHz core.
- **Internal heap dips to ~60 KB** while rendering (vs ~105 KB between frames) —
  ThorVG's RLE span allocations are small enough to land in internal RAM. This
  is the tightest resource in the project; watch it after any scene change.
- **`stats` cannot report another scene's frame time.** Opening the port resets
  the chip, so a bare `stats` always reports the default scene. Read a scene's
  frame time off the panel in a `shot --scene <name>` instead.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Cannot open /dev/ttyACM0` | Board unplugged, or `idf.py monitor` is holding the port. |
| `pyserial not found` | IDF venv missing — run `install.sh esp32p4`. |
| `no <<<SHOT>>> frame in N bytes` | Console task stack too small, or logging not silenced during capture. |
| `base64 decode failed: Incorrect padding` | Log output interleaved into the image stream. |
| Screenshot colours look brown/orange | The B,G,R swap was skipped. |
| Board reboots mid-screenshot, no panic text | Stack overflow in the console task. |
| `smoke` fails on `scene built` | Canvas allocation failed — check PSRAM came up at 200 MHz in the boot log. |
