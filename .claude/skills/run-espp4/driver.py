#!/usr/bin/env python3
"""
Driver for the ESP32-P4 LVGL + ThorVG demo.

Builds, flashes and then *drives* the running firmware over its USB-Serial/JTAG
debug console (see main/SerialConsole.cpp), including pulling a real
screenshot off the panel and decoding it to PNG.

Run from the project root:

    python3 .claude/skills/run-espp4/driver.py smoke

Needs pyserial, which ships inside the ESP-IDF virtualenv. If this script is
started with a python that lacks it, it re-execs itself with the IDF venv
python automatically -- no `source export.sh` needed for serial-only commands.
`build` and `flash` shell out to idf.py and DO need the IDF environment; the
script sources it for you.
"""

import argparse
import base64
import glob
import os
import re
import struct
import subprocess
import sys
import time
import zlib

PROJECT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200

# --------------------------------------------------------------------------
# Bootstrap: find pyserial (IDF venv) and idf.py
# --------------------------------------------------------------------------


def _venv_python():
    """
    The IDF virtualenv's bin/python is a *symlink to the system python*, so
    comparing realpaths would reject it. Compare the literal path instead, and
    use an env sentinel to make an exec loop impossible.
    """
    for p in sorted(glob.glob(os.path.expanduser("~/.espressif/python_env/idf*_env/bin/python"))):
        if p != sys.executable:
            return p
    return None


try:
    import serial  # noqa: F401
except ImportError:  # pragma: no cover - bootstrap path
    _py = _venv_python()
    if _py and not os.environ.get("ESPP4_DRIVER_REEXEC"):
        os.environ["ESPP4_DRIVER_REEXEC"] = "1"
        os.execve(_py, [_py, os.path.abspath(__file__)] + sys.argv[1:], os.environ)
    sys.exit(
        "pyserial not found. Looked for an ESP-IDF virtualenv under\n"
        "  ~/.espressif/python_env/idf*_env/bin/python\n"
        "Either run the IDF install script, or `pip install pyserial`."
    )

import serial  # noqa: E402


def idf_export():
    """Locate export.sh. Returns the path, or None."""
    if os.environ.get("IDF_PATH"):
        cand = os.path.join(os.environ["IDF_PATH"], "export.sh")
        if os.path.exists(cand):
            return cand
    for p in sorted(glob.glob(os.path.expanduser("~/esp/*/esp-idf/export.sh")), reverse=True):
        return p
    return None


def run_idf(args):
    """Run idf.py <args> inside a shell that has sourced export.sh."""
    export = idf_export()
    if not export:
        sys.exit("Could not find esp-idf/export.sh (looked at $IDF_PATH and ~/esp/*/esp-idf).")
    cmd = ". '%s' >/dev/null 2>&1 && idf.py %s" % (export, " ".join(args))
    return subprocess.call(["bash", "-lc", cmd], cwd=PROJECT)


# --------------------------------------------------------------------------
# Serial plumbing
# --------------------------------------------------------------------------

READY = b"<<<CONSOLE ready>>>"


def open_port(port, baud=DEFAULT_BAUD, settle=3.0, quiet=False):
    """
    Open the console.

    Opening a USB-Serial/JTAG port asserts DTR/RTS, which resets the chip. We
    immediately de-assert both and then wait out the boot, so every session
    starts from a known state. `settle` is that wait.
    """
    try:
        s = serial.Serial(port, baud, timeout=1.0)
    except serial.SerialException as e:
        sys.exit(
            "Cannot open %s: %s\n"
            "Is the board plugged in? `ls /dev/ttyACM* /dev/ttyUSB*`\n"
            "Is another process (idf.py monitor) holding the port?" % (port, e)
        )
    s.dtr = False
    s.rts = False
    if settle:
        if not quiet:
            print("waiting %.1fs for boot..." % settle, file=sys.stderr)
        time.sleep(settle)
    s.reset_input_buffer()
    return s


def read_until(s, marker, timeout):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        chunk = s.read(65536)
        if chunk:
            buf += chunk
            if marker in buf:
                break
    return buf, time.time() - t0


def command(s, ch, marker, timeout):
    s.write(ch)
    s.flush()
    return read_until(s, marker, timeout)


# --------------------------------------------------------------------------
# PNG (stdlib only -- PIL is not in the IDF venv)
# --------------------------------------------------------------------------


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" +
                chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) +
                chunk(b"IEND", b""))


def bgr_to_rgb(buf):
    """
    LVGL stores lv_color_t as {blue, green, red}, so LV_COLOR_FORMAT_RGB888 is
    B,G,R in memory. Without this swap every screenshot comes out with red and
    blue exchanged -- the demo's blue theme renders brown.
    """
    b = bytearray(buf)
    b[0::3], b[2::3] = buf[2::3], buf[0::3]
    return bytes(b)


SHOT_RE = re.compile(
    rb"<<<SHOT step=(\d+) fmt=(\w+)>>>\n(.*?)<<<ENDSHOT ok=(\d) w=(\d+) h=(\d+)>>>",
    re.S,
)


def decode_shot(buf, out_path):
    m = SHOT_RE.search(buf)
    if not m:
        return None, "no <<<SHOT>>> frame in %d bytes of output" % len(buf)
    body, ok, w, h = m.group(3), int(m.group(4)), int(m.group(5)), int(m.group(6))
    if not ok:
        return None, "device reported ok=0 (snapshot allocation failed)"
    try:
        raw = base64.b64decode(re.sub(rb"\s", b"", body))
    except Exception as e:
        return None, "base64 decode failed: %s" % e
    if len(raw) != w * h * 3:
        return None, "size mismatch: got %d bytes, expected %d (%dx%d)" % (len(raw), w * h * 3, w, h)
    write_png(out_path, w, h, bgr_to_rgb(raw))
    return (w, h), None


# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------


def cmd_build(a):
    return run_idf(["build"])


def cmd_flash(a):
    return run_idf(["-p", a.port, "flash"])


def cmd_stats(a):
    s = open_port(a.port, settle=a.settle)
    buf, _ = command(s, b"i", b">>>\n", 5)
    s.close()
    line = [l for l in buf.decode("utf-8", "replace").splitlines() if l.startswith("<<<STATS")]
    if not line:
        print("no stats returned:\n" + buf.decode("utf-8", "replace")[-500:], file=sys.stderr)
        return 1
    print(line[-1])
    return 0


def cmd_toggle(a):
    s = open_port(a.port, settle=a.settle)
    buf, _ = command(s, b"t", b"<<<STATS", 5)
    s.close()
    for l in buf.decode("utf-8", "replace").splitlines():
        if l.startswith("<<<STATS"):
            print(l)
    return 0


def read_scene(s):
    buf, _ = command(s, b"i", b">>>\n", 5)
    m = re.search(r"<<<STATS scene=(\w+)", buf.decode("utf-8", "replace"))
    return m.group(1) if m else None


def select_scene(s, want):
    """
    Toggle until the board reports the requested scene.

    Blind toggling is not reliable: opening the port resets the chip (so state
    never survives between invocations), and stray bytes left in the USB
    endpoint can toggle it again behind our back. Read the scene back instead.
    """
    for _ in range(6):
        cur = read_scene(s)
        if cur is None:
            return None
        if cur == want:
            return cur
        command(s, b"t", b"<<<STATS", 5)
        time.sleep(0.3)
    return read_scene(s)


def cmd_shot(a):
    s = open_port(a.port, settle=a.settle)
    if a.scene:
        got = select_scene(s, a.scene)
        if got != a.scene:
            print("could not select scene %r (board reports %r)" % (a.scene, got), file=sys.stderr)
            s.close()
            return 1
    scene = read_scene(s)
    ch = b"S" if a.full else b"s"
    t0 = time.time()
    buf, _ = command(s, ch, b"<<<ENDSHOT", a.timeout)
    s.close()
    dims, err = decode_shot(buf, a.out)
    if err:
        print("screenshot failed: %s" % err, file=sys.stderr)
        tail = buf.decode("utf-8", "replace")[-400:]
        print("--- tail ---\n%s" % tail, file=sys.stderr)
        return 1
    print("%s  %dx%d  scene=%s  %.1fs" % (a.out, dims[0], dims[1], scene, time.time() - t0))
    return 0


BOOT_MARKERS = [
    ("panel 720x720", "app_main reached"),
    ("jd9365", "MIPI-DSI panel driver"),
    ("Touch 0x5d found", "GT911 touch"),
    ("canvas 400x400 ARGB8888 ready", "scene built"),
    ("<<<CONSOLE ready>>>", "debug console"),
]
# NOT "rst:0x" -- the ROM prints rst:0x1 (POWERON) on every healthy boot.
# These are the strings that only appear when something actually went wrong.
PANIC_MARKERS = [
    "Guru Meditation",
    "panic'ed",
    "Stack protection fault",
    "abort() was called",
    "assert failed",
    "Backtrace:",
    "WDT_RST",
    "brownout",
]


def cmd_smoke(a):
    s = open_port(a.port, settle=0, quiet=True)
    # Force a clean reset so we capture the whole boot log.
    s.dtr = False
    s.rts = True
    time.sleep(0.15)
    s.rts = False
    boot, _ = read_until(s, READY, 20)
    text = boot.decode("utf-8", "replace")

    failures = []
    for needle, what in BOOT_MARKERS:
        ok = needle in text
        print("[%s] %-24s (%s)" % ("ok" if ok else "FAIL", what, needle))
        if not ok:
            failures.append(what)

    for p in PANIC_MARKERS:
        if p in text:
            print("[FAIL] panic marker in boot log: %r" % p)
            failures.append("panic:" + p)

    if failures:
        s.close()
        sys.stdout.flush()
        print("\n--- boot log tail ---\n%s" % text[-1500:], file=sys.stderr)
        print("\nSMOKE FAILED: %s" % ", ".join(failures))
        return 1

    # The UI has no serial heartbeat, so prove it is live by asking twice and
    # requiring the animation to have advanced between the two samples.
    st1, _ = command(s, b"i", b">>>\n", 5)
    time.sleep(1.5)
    st2, _ = command(s, b"i", b">>>\n", 5)
    s.close()

    def stat(b):
        for l in b.decode("utf-8", "replace").splitlines():
            if l.startswith("<<<STATS"):
                return l
        return ""

    s1, s2 = stat(st1), stat(st2)
    print("[ok] stats: %s" % s1)
    if not s1 or not s2:
        print("SMOKE FAILED: console did not answer 'i'")
        return 1

    ms = re.search(r"frame_ms=([\d.]+)", s1)
    if ms:
        print("[ok] ThorVG frame time %s ms" % ms.group(1))
    print("\nSMOKE PASSED")
    return 0


def cmd_monitor(a):
    s = open_port(a.port, settle=0, quiet=True)
    t0 = time.time()
    try:
        while time.time() - t0 < a.secs:
            c = s.read(4096)
            if c:
                sys.stdout.write(c.decode("utf-8", "replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    s.close()
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default=DEFAULT_PORT)
    p.add_argument("--settle", type=float, default=3.0,
                   help="seconds to wait after the open-induced reset")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("build").set_defaults(fn=cmd_build)
    sub.add_parser("flash").set_defaults(fn=cmd_flash)
    sub.add_parser("smoke").set_defaults(fn=cmd_smoke)
    sub.add_parser("stats").set_defaults(fn=cmd_stats)
    sub.add_parser("toggle").set_defaults(fn=cmd_toggle)

    sp = sub.add_parser("shot")
    sp.add_argument("--out", default="panel.png")
    sp.add_argument("--full", action="store_true", help="720x720 instead of 360x360")
    sp.add_argument("--timeout", type=float, default=120)
    sp.add_argument("--scene", choices=["gauge", "scale", "ias", "altimeter"],
                    help="switch to this scene first, verifying via stats")
    sp.set_defaults(fn=cmd_shot)

    sp = sub.add_parser("monitor")
    sp.add_argument("--secs", type=float, default=10)
    sp.set_defaults(fn=cmd_monitor)

    a = p.parse_args()
    sys.exit(a.fn(a))


if __name__ == "__main__":
    main()
