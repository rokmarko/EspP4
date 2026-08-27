#pragma once

/**
 * @file SerialConsole.h
 * @brief One-character debug console on USB-Serial/JTAG.
 *
 * Exists so the board can be driven programmatically from a host: see
 * .claude/skills/run-espp4/driver.py. Compile it out by building with
 * -DDEMO_NO_SERIAL_CONSOLE.
 */

namespace demo {

/** Install the USB-Serial/JTAG driver and start the console task. */
void StartSerialConsole();

} // namespace demo
