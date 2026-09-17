/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// The platform surface on a desktop.
//
// Everything the board gets from ESP-IDF, in POSIX and the C++ standard
// library. Two of them are worth reading before using the simulator:
//
//  - **The heap numbers are zeros.** A desktop has no 400 kB of internal RAM
//    to run out of, and reporting glibc's arena sizes in those fields would
//    invite comparisons that mean nothing. The console prints them as zero and
//    the host tooling reads that as "not measured here".
//  - **The console owns stdout, and nothing else may write to it.** The
//    protocol the host parses -- <<<STATS>>> lines, base64 screenshot bodies --
//    goes out on the process's original stdout, and ConsoleOpen() then points
//    the stdout *descriptor* at stderr so that anything printf()ing lands in
//    the log instead. Common's PRINTF does exactly that (Defines.h routes it
//    to printf off the board), and one such line in the middle of a base64
//    body corrupts the image beyond recovery on the host. On the board the
//    same problem is solved the other way round: everything shares the USB
//    endpoint and the console silences logging for the duration of a capture.

#include "Platform.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <termios.h>
#include <unistd.h>

namespace platform {

namespace {

	constexpr const char* TAG = "platform";

	std::atomic<LogLevel> g_eLogLevel{LogLevel::Info};

// The LVGL lock. Recursive because the scene's tick can reach the console,
// which takes it again, exactly as the BSP's mutex allows on the board.
	std::recursive_mutex g_mxDisplay;

// Boot, as Micros() counts from.
	const std::chrono::steady_clock::time_point g_tStart = std::chrono::steady_clock::now();

// Threads live as long as the process and nothing ever joins them, so they are
// detached: a joinable std::thread destroyed at exit calls std::terminate, and
// exit is exactly what happens when the SDL window closes. The record outlives
// the thread so a TaskHandle stays valid to hand back to StackHeadroom().
	struct Task
	{
		std::string ssName;
	};

	std::mutex								  g_mxTasks;
	std::vector<std::unique_ptr<Task>> g_vTasks;

// The host link: a duplicate of the original stdout, taken before that
// descriptor is pointed elsewhere. -1 until ConsoleOpen().
	int g_fdConsole = -1;

// The terminal settings ConsoleOpen() replaced, so they can be put back.
	termios g_ttyOld{};
	bool	  g_bTtyChanged = false;

	void RestoreTty()
	{
		if(g_bTtyChanged) {
			tcsetattr(STDIN_FILENO, TCSANOW, &g_ttyOld);
			g_bTtyChanged = false;
		}
	}

	char LevelChar(LogLevel eLevel)
	{
		switch(eLevel) {
		case LogLevel::None:	 return ' ';
		case LogLevel::Error: return 'E';
		case LogLevel::Warn:	 return 'W';
		case LogLevel::Info:	 return 'I';
		case LogLevel::Debug: return 'D';
		}
		return '?';
	}

} // namespace

// --------------------------------------------------------------------------

const char* Name()
{
	return "sim";
}

// --------------------------------------------------------------------------

void Log(LogLevel eLevel, const char* pszTag, const char* pszFmt, ...)
{
	if(static_cast<uint8_t>(eLevel) > static_cast<uint8_t>(g_eLogLevel.load()))
		return;

	char	  szLine[512];
	va_list args;
	va_start(args, pszFmt);
	std::vsnprintf(szLine, sizeof(szLine), pszFmt, args);
	va_end(args);

	// The shape ESP-IDF prints, so a log from the simulator and a log from the
	// board read the same and the same eye can scan both.
	const long lMs = static_cast<long>(Micros() / 1000);
	std::fprintf(stderr, "%c (%ld) %s: %s\n", LevelChar(eLevel), lMs, pszTag, szLine);
}

// --------------------------------------------------------------------------

void SetLogLevel(LogLevel eLevel)
{
	g_eLogLevel.store(eLevel);
}

// --------------------------------------------------------------------------

LogLevel GetLogLevel()
{
	return g_eLogLevel.load();
}

// --------------------------------------------------------------------------

int64_t Micros()
{
	const auto dt = std::chrono::steady_clock::now() - g_tStart;
	return std::chrono::duration_cast<std::chrono::microseconds>(dt).count();
}

// --------------------------------------------------------------------------

void SleepMs(uint32_t uMs)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(uMs));
}

// --------------------------------------------------------------------------

void LockDisplay()
{
	g_mxDisplay.lock();
}

// --------------------------------------------------------------------------

void UnlockDisplay()
{
	g_mxDisplay.unlock();
}

// --------------------------------------------------------------------------

HeapStats GetHeapStats()
{
	return {};
}

// --------------------------------------------------------------------------

TaskHandle StartTask(const TaskConfig& cfg, void (*pfnEntry)(void*), void* pCtx)
{
	// The stack size and the priority are the board's problem. Asking a
	// desktop scheduler for either would change what the simulator proves
	// rather than what it costs.
	auto pTask	  = std::make_unique<Task>();
	pTask->ssName = cfg.pszName;

	std::thread(pfnEntry, pCtx).detach();

	std::lock_guard g(g_mxTasks);
	g_vTasks.push_back(std::move(pTask));
	return static_cast<TaskHandle>(g_vTasks.back().get());
}

// --------------------------------------------------------------------------

uint32_t StackHeadroom(TaskHandle /*hTask*/)
{
	// Nothing to report: a desktop thread's stack grows into virtual address
	// space and the number would mean nothing next to the board's.
	return 0;
}

// --------------------------------------------------------------------------

// A desktop's network is the operating system's business and is either there
// or not; there is nothing for this build to bring up, and nothing it could
// usefully say about it that the machine does not already know.
bool NetworkStart()
{
	return true;
}

bool IsNetworkUp()
{
	return true;
}

const char* NetworkStatus()
{
	return "host";
}

// --------------------------------------------------------------------------

bool ConsoleOpen()
{
	if(g_fdConsole >= 0)
		return true;

	// Take the host link away from everything else before anything can write to
	// it -- see the note at the top of this file.
	g_fdConsole = ::dup(STDOUT_FILENO);
	if(g_fdConsole < 0)
		return false;
	if(::dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
		return false;

	// Unbuffered, so a reply reaches the host as soon as it is written rather
	// than when the pipe buffer happens to fill.
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	std::setvbuf(stderr, nullptr, _IONBF, 0);

	// A pipe needs nothing further; a terminal needs the line discipline out of
	// the way, because the console reads one character at a time and echoing it
	// would corrupt its own output.
	if(isatty(STDIN_FILENO) == 0)
		return true;

	if(tcgetattr(STDIN_FILENO, &g_ttyOld) != 0) {
		APP_LOGW(TAG, "stdin is a terminal but will not describe itself; console may echo");
		return true;
	}

	termios tty = g_ttyOld;
	tty.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
	tty.c_cc[VMIN]	 = 1;
	tty.c_cc[VTIME] = 0;
	if(tcsetattr(STDIN_FILENO, TCSANOW, &tty) != 0) {
		APP_LOGW(TAG, "cannot put stdin in raw mode; console may echo");
		return true;
	}

	g_bTtyChanged = true;
	std::atexit(RestoreTty);
	return true;
}

// --------------------------------------------------------------------------

size_t ConsoleRead(uint8_t* pData, size_t uSize)
{
	const ssize_t n = ::read(STDIN_FILENO, pData, uSize);
	return n > 0 ? static_cast<size_t>(n) : 0;
}

// --------------------------------------------------------------------------

void ConsoleWrite(const uint8_t* pData, size_t uSize)
{
	if(g_fdConsole < 0)
		return;

	while(uSize > 0) {
		const ssize_t n = ::write(g_fdConsole, pData, uSize);
		if(n <= 0)
			return;  // the host is not reading; dropping is better than hanging
		pData += n;
		uSize -= static_cast<size_t>(n);
	}
}

} // namespace platform
