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
python3 .claude/skills/run-espp4/driver.py shot --scene rpm       --out rpm.png
```

`smoke` resets the board, checks five boot markers, fails on any panic marker,
then proves the UI is live by reading stats:

```
[ok] app_main reached         (panel 720x720)
[ok] MIPI-DSI panel driver    (jd9365)
[ok] GT911 touch              (Touch 0x5d found)
[ok] scene built              (canvas 400x400 ARGB8888 ready)
[ok] debug console            (<<<CONSOLE ready>>>)
[ok] stats: <<<STATS scene=gauge frame_ms=65.5 heap_int=82323 heap_psram=28772900 rpm=2037 eng=1 moving=0 model_stack=7352 can=self-test can_rx=76 can_tx=76 can_nod=72 can_alive=1 can_ident=0 can_err=0 can_state=1 nvs=open nvs_opt=4 nvs_used=34/756 heap_int_min=32607 can_push=0>>>
[ok] ThorVG frame time 65.5 ms

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

The `nvs_*` fields cover the option store. `nvs_opt` is how many option blobs
came back out of NVS at boot: **0 only on the first boot after the `settings`
partition is erased** (the defaults are written then), 4 on every boot after
that. `nvs_used` is NVS entries used of the partition's capacity — 34 covers the four
option blobs plus the single packed parameter blob. `w` forces
the whole set out and prints `<<<SAVE>>>`; reboot afterwards and check
`nvs_opt` to prove a round trip.

`P` exercises the whole parameter-transfer path over the self-test loopback,
both halves: it pushes one packed parameter through `OldServices` exactly as
Nesis does — a DDS_BUFFER download, then the MCS_APPLY_BUFFER_DATA that commits
it — and receives it back through DDS_B and MCS_B. The board applies it, saves the
container to NVS and refreshes the scale bands — so the tachometer's green band
moves from 2500 to 2200 rpm and **stays there across a reboot**. `can_push` in
`<<<STATS>>>` counts accepted pushes since boot. Watch for these in the log:

```
canproc: pushing 368 bytes to node 22 in 92 messages    <- DDS_A offered it
canproc: accepting a 368 byte buffer push               <- DDS_B took it
canproc: parameter push accepted: 368 bytes, CRC 0xebcb <- MCS_B verified it
canproc: node 22 accepted config 14                     <- MCS_A saw the answer
params:  parameter 500 updated from the bus: 4 bands, tc 400 ms
settings: 4 parameters stored as a 605 B blob
scene:   scale bands refreshed from the parameter container
```

The transfer is asynchronous — `OldServices::Update()` posts one message per
call and the model task pumps it every 50 ms, so 92 messages take about 600 ms.
`P` waits for the services to go idle rather than guessing at a delay.

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
| `i` | `<<<STATS scene=… frame_ms=… heap_int=… heap_psram=… rpm=… eng=… moving=… model_stack=… can_*=… nvs=… nvs_opt=… nvs_used=…>>>` |
| `w` | write the option blobs to NVS, then `<<<SAVE ok=… written=… used=…>>>` |
| `P` | push a parameter at ourselves over CAN, then `<<<PUSH ok=… pushes=…>>>` |
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
  one session. The scenes cycle gauge -> scale -> ias -> altimeter -> rpm.
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

- **Per ThorVG frame: gauge ~60 ms, rpm ~58 ms, ias ~38 ms, scale ~34 ms, altimeter ~28 ms.**
  The animation timer asks for 33 ms, so the gauge runs at ~16 fps and the
  instrument scenes roughly reach the timer. This is software rasterisation of a
  400×400 ARGB8888 canvas on a 360 MHz core.
- **Internal heap: low-water mark ~32 KB, largest contiguous block ~31 KB.**
  ThorVG's RLE span allocations are small enough to land in internal RAM. This
  is the tightest resource in the project. **Read `heap_int_min`, not
  `heap_int`** — the latter is sampled at a random point in a ThorVG frame and
  swings between ~35 KB and ~80 KB, which looks like a regression when it is
  only sampling noise. Watch the *largest block* too (app_main logs it at
  boot): the LVGL task, the console task and the CAN thread each need a
  contiguous 32 KB, and there is no room for a fourth.
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
| No `<<<CONSOLE ready>>>`, board otherwise boots fine | The console task's 32 KB stack could not be allocated. Check the boot log for `could not create the console task` and the `largest block` figure; something ahead of it fragmented internal RAM. |
| `pthread: Failed to create task` then `abort()` | Same cause, hitting the CAN thread instead. Usually an enlarged `settings` NVS partition — see partitions.csv. |
| `P` reports `ok=0` | The CAN port is not in self-test — a real bus refuses to loop our own frames back. |
| `apply-buffer CRC mismatch` after a push | Frames were dropped between the download and the commit. The RX queue is 32 deep; `Pump()` bounds its burst to keep the receive thread ahead. |
| `push to node N failed: <state>` | DDS_A's own state machine gave up — the text is `DDS_A::GetStateText()`, so it names the step (no confirm, timeout, node B busy, CRC). |
| `smoke` fails on `scene built` | Canvas allocation failed — check PSRAM came up at 200 MHz in the boot log. |
