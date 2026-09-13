/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2019 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *   Writen by:                                                            *
 *      Rok Markovic [rok.markovic@kanardia.eu]                            *
 *                                                                         *
 *   Status: Open Source                                                   *
 *                                                                         *
 *   License: GPL - GNU General Public License                             *
 *                                                                         *
 ***************************************************************************/

#pragma once

// Everything the application asks of the machine underneath it.
//
// src/ is written once and built twice: for the ESP32-P4 panel out of
// port/esp, and for a desktop simulator out of port/pc. This header is the
// seam. Nothing above it names ESP-IDF, FreeRTOS, SDL or POSIX; each port
// supplies one translation unit that does.
//
// It is a plain function surface rather than a class, because there is exactly
// one machine per build and a virtual call per log line would be silly. The
// two things that do carry state -- the firmware slot and the settings store
// -- are interfaces, since they have a lifetime and can fail.
//
// Logging is the one place where the ports differ in shape rather than in
// implementation: on the board APP_LOG* is ESP-IDF's own macro, so the tag
// colouring, the per-tag levels and the timestamps are exactly what they have
// always been, and nothing in the boot log moved. On a desktop it lands on
// platform::Log(), which writes the same "I (tag) text" shape to stderr.

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace platform {

// --------------------------------------------------------------------------
//  Identity
// --------------------------------------------------------------------------

// Short name of the machine this build runs on: "esp32p4" or "sim". Shown on
// the scene's title line, so a screenshot says which one produced it.
const char* Name();

// --------------------------------------------------------------------------
//  Logging
// --------------------------------------------------------------------------

enum class LogLevel : uint8_t
{
	None	= 0,
	Error = 1,
	Warn	= 2,
	Info	= 3,
	Debug = 4,
};

// The desktop back end. On the board APP_LOG* goes straight to ESP-IDF and
// this is never reached.
void Log(LogLevel eLevel, const char* pszTag, const char* pszFmt, ...) __attribute__((format(printf, 3, 4)));

// Silence everything below eLevel, on whichever logger this build uses.
//
// The console's screenshot command needs this: the base64 body and the log
// share one output stream, and a line emitted mid-capture corrupts the image
// beyond recovery on the host.
void SetLogLevel(LogLevel eLevel);

// --------------------------------------------------------------------------
//  Time
// --------------------------------------------------------------------------

// Microseconds since boot, monotonic.
int64_t Micros();

// Block the calling thread. Whole milliseconds; this is a pacing primitive,
// not a timer.
void SleepMs(uint32_t uMs);

// --------------------------------------------------------------------------
//  The LVGL lock
// --------------------------------------------------------------------------

// LVGL is not thread-safe and every build here drives it from more than one
// thread: the console asks for a screenshot, the model loop changes scene.
//
// On the board this is the BSP's own mutex, the one esp_lvgl_adapter takes
// around its task; in the simulator it is ours, taken around the loop in
// MainSim.cpp. Recursive in both, because the scene's tick can end up here
// through the console.
void LockDisplay();
void UnlockDisplay();

// Scope guard for the above.
class DisplayLock
{
public:
	DisplayLock() { LockDisplay(); }
	~DisplayLock() { UnlockDisplay(); }

	DisplayLock(const DisplayLock&)				 = delete;
	DisplayLock& operator=(const DisplayLock&) = delete;
};

// --------------------------------------------------------------------------
//  Memory
// --------------------------------------------------------------------------

// What the console's `i` line reports. Internal RAM is the tightest resource
// on the board, and the low-water mark is the figure that says whether the
// margin is real -- the free size is a snapshot taken at a random point in a
// ThorVG frame and swings by tens of kB.
//
// A desktop has none of these limits; the simulator answers with zeros rather
// than inventing numbers, and the host-side tooling reads them as "not
// measured here".
struct HeapStats
{
	uint32_t uFreeInternal	  = 0;
	uint32_t uFreePsram		  = 0;
	uint32_t uMinFreeInternal = 0;
	uint32_t uLargestBlock	  = 0;
};

HeapStats GetHeapStats();

// --------------------------------------------------------------------------
//  Threads
// --------------------------------------------------------------------------

// Opaque; a FreeRTOS task handle on the board, a heap-allocated record in the
// simulator. Only ever passed back to StackHeadroom().
using TaskHandle = void*;

struct TaskConfig
{
	const char* pszName = "task";
	// Bytes. Honoured on the board, where it is the whole point; a desktop
	// thread gets the system default and this is ignored.
	uint32_t uStack = 8 * 1024;
	// Higher is more urgent, FreeRTOS sense. Ignored in the simulator.
	int iPriority = 4;
};

// Start a thread that runs until the process does. Returns nullptr on failure.
TaskHandle StartTask(const TaskConfig& cfg, void (*pfnEntry)(void*), void* pCtx);

// Smallest free stack that task has ever had, in bytes. Zero when the port
// cannot measure it, which is the desktop's answer.
uint32_t StackHeadroom(TaskHandle hTask);

// --------------------------------------------------------------------------
//  The host link
// --------------------------------------------------------------------------

// The byte stream the one-character debug console lives on: USB-Serial/JTAG on
// the board, stdin/stdout in the simulator. See SerialConsole.cpp for the
// protocol -- it is the same one on both, so the run skill's driver.py drives
// either.

// Install the driver and put the stream in the state the console needs
// (unbuffered, no echo, no line discipline). Called once, before the task.
bool ConsoleOpen();

// Block until at least one byte arrives. Returns how many were read, or 0 if
// the stream ended -- which is how the simulator's console learns to stop.
size_t ConsoleRead(uint8_t* pData, size_t uSize);

// Write all of it, or give up. Never partial-writes back to the caller.
void ConsoleWrite(const uint8_t* pData, size_t uSize);

// --------------------------------------------------------------------------
//  Firmware update
// --------------------------------------------------------------------------

// Where the bytes of a firmware update pushed over CAN end up.
//
// CanProcessor implements the CANaerospace side of APS_B -- accept the pages,
// check their CRC, count them -- and hands each verified 2 kB page here. On
// the board this is ESP-IDF's OTA API writing the app slot that is not
// running; in the simulator it is a file, so the whole transfer can be driven
// and the result diffed against the image that was sent.
class FirmwareTarget
{
public:
	virtual ~FirmwareTarget() = default;

	// Open a transfer of uPages 2 kB pages. Any transfer already open is
	// abandoned first -- a sender that restarts just sends another start.
	//
	// Returns false if there is nowhere to write, in which case Write() and
	//         Finish() do nothing.
	virtual bool Begin(uint32_t uPages) = 0;

	// One verified page, in order. Returns false on a write error; the
	// transfer is abandoned by the caller when that happens.
	virtual bool Write(const uint8_t* pData, uint32_t uSize) = 0;

	// Close the transfer and make it the image that runs next. Returns false
	// if what landed does not validate -- pages that each passed their CRC can
	// still add up to a corrupt image.
	virtual bool Finish() = 0;

	// Give up on the transfer in progress and release whatever it held.
	virtual void Abort() = 0;

	// Where the bytes are going, for the log line: a partition label on the
	// board, a file name in the simulator.
	virtual const char* GetName() const = 0;

	// True while a transfer is open.
	virtual bool IsOpen() const = 0;
};

// The one firmware target this build writes through.
FirmwareTarget& GetFirmwareTarget();

} // namespace platform

// --------------------------------------------------------------------------
//  APP_LOG*
// --------------------------------------------------------------------------
//
// Deliberately below the namespace: on the board these are ESP-IDF's macros
// verbatim, so the board's log is byte for byte what it was before the port
// existed, and esp_log.h has to be in scope.

#if defined(ESP_PLATFORM)

#include "esp_log.h"

#define APP_LOGE(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
#define APP_LOGW(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
#define APP_LOGI(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define APP_LOGD(tag, ...) ESP_LOGD(tag, __VA_ARGS__)

#else

#define APP_LOGE(tag, ...) ::platform::Log(::platform::LogLevel::Error, tag, __VA_ARGS__)
#define APP_LOGW(tag, ...) ::platform::Log(::platform::LogLevel::Warn, tag, __VA_ARGS__)
#define APP_LOGI(tag, ...) ::platform::Log(::platform::LogLevel::Info, tag, __VA_ARGS__)
#define APP_LOGD(tag, ...) ::platform::Log(::platform::LogLevel::Debug, tag, __VA_ARGS__)

#endif
