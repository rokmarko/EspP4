/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// The one-character debug console.
//
// USB-Serial/JTAG on the board, stdin/stdout in the simulator -- the bytes go
// through platform::Console*(), so the protocol below is the same on both and
// the run skill's driver.py drives either.
//
// Commands (send a single byte, no newline needed):
//
//   h   help
//   i   print one <<<STATS ...>>> line
//   t   toggle scene, as a screen tap would
//   m   move the settings page's selection one row down, wrapping
//   M   activate what the selection rests on, as a tap would
//
// The settings page also takes the terminal's own keys: the arrows, Enter and
// Esc, decoded below. `m` and `M` are the unambiguous aliases a script wants.
//   w   write the option blobs to NVS, then print <<<SAVE ...>>>
//   P   push a parameter at ourselves over CAN (self-test only)
//   s   screenshot at half resolution  (fast, ~360x360)
//   S   screenshot at full resolution  (720x720, several seconds)
//
// A screenshot is framed so a host parser can find it in the log stream:
//
//   <<<SHOT w=360 h=360 fmt=rgb888>>>
//   <base64, 76 chars per line>
//   <<<ENDSHOT>>>

#include "SerialConsole.h"

#ifndef DEMO_NO_SERIAL_CONSOLE

#include "AppModel.h"
#include "AppOptions.h"
#include "CanPort.h"
#include "CanProcessor.h"
#include "Platform.h"
#include "MenuPage.h"
#include "StorageOptions.h"
#include "VectorScene.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr const char* TAG = "console";

constexpr int CONSOLE_TASK_STACK = 32768; // LVGL draw calls happen on this task
constexpr int B64_LINE_LEN			= 76; // must stay a multiple of 4

void WriteAll(const uint8_t* data, size_t len)
{
	platform::ConsoleWrite(data, len);
}

void WriteStr(const char* s)
{
	WriteAll(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

// Streaming base64 encoder. Rows arrive in arbitrary sizes, so leftover
// bytes carry across calls; Finish() flushes the tail with padding.
class Base64Stream
{
public:
	void Feed(const uint8_t* data, size_t len)
	{
		for(size_t i = 0; i < len; i++) {
			m_group[m_nGroup++] = data[i];
			if(m_nGroup == 3) {
				EmitGroup(3);
				m_nGroup = 0;
			}
		}
	}

	void Finish()
	{
		if(m_nGroup > 0) {
			const int pad = 3 - m_nGroup;
			for(int i = m_nGroup; i < 3; i++)
				m_group[i] = 0;
			EmitGroup(3 - pad);
			m_nGroup = 0;
		}
		if(m_nLine > 0)
			PutRaw("\n", 1);
		Flush();
	}

private:
	static constexpr const char* ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	void EmitGroup(int valid)
	{
		char				out[4];
		const uint32_t v = (static_cast<uint32_t>(m_group[0]) << 16) | (static_cast<uint32_t>(m_group[1]) << 8)
								 | static_cast<uint32_t>(m_group[2]);
		out[0] = ALPHABET[(v >> 18) & 0x3F];
		out[1] = ALPHABET[(v >> 12) & 0x3F];
		out[2] = valid > 1 ? ALPHABET[(v >> 6) & 0x3F] : '=';
		out[3] = valid > 2 ? ALPHABET[v & 0x3F] : '=';
		PutRaw(out, 4);

		m_nLine += 4;
		if(m_nLine >= B64_LINE_LEN) {
			PutRaw("\n", 1);
			m_nLine = 0;
		}
	}

	void PutRaw(const char* s, size_t n)
	{
		if(m_nBuf + n > sizeof(m_buf))
			Flush();
		std::memcpy(m_buf + m_nBuf, s, n);
		m_nBuf += n;
	}

	void Flush()
	{
		if(m_nBuf == 0)
			return;
		WriteAll(m_buf, m_nBuf);
		m_nBuf = 0;
	}

	uint8_t m_group[3]  = {};
	int	  m_nGroup	  = 0;
	int	  m_nLine	  = 0;
	uint8_t m_buf[1024] = {};
	size_t  m_nBuf		  = 0;
};

void SinkToBase64(const uint8_t* data, size_t len, void* ctx)
{
	static_cast<Base64Stream*>(ctx)->Feed(data, len);
}

void PrintStats()
{
	char		 line[448];
	const int tenths = demo::FrameTimeTenths();

	// Model fields prove the processing loop is actually ticking: rpm comes
	// from the NOD, eng/moving from ModelBase's above/below detectors.
	const app::Model*			 pModel	= app::GetModel();
	const app::CanPort*		 pPort	= app::GetCanPort();
	const app::CanProcessor* pProc	= app::GetCanProcessor();
	const int					 iRpm		= pModel ? static_cast<int>(pModel->GetEngineRPM() + 0.5f) : -1;
	const int					 iEng		= pModel ? (pModel->IsEngineRunning() ? 1 : 0) : -1;
	const int					 iMoving = pModel ? (pModel->IsMoving() ? 1 : 0) : -1;

	// nvs_opt is how many option blobs came back at boot; a fresh store reads
	// 0 and writes the defaults, every boot after that reads them all.
	const app::Settings::Usage use	= app::GetSettings().GetUsage();
	const bool						bOpen = app::GetSettings().IsOpen();

	// Zeros in the simulator, which has none of the board's memory limits --
	// see port/pc/PlatformSim.cpp.
	const platform::HeapStats heap = platform::GetHeapStats();

	std::snprintf(
		line,
		sizeof(line),
		"<<<STATS scene=%s frame_ms=%d.%d heap_int=%u heap_psram=%u "
		"rpm=%d eng=%d moving=%d model_stack=%u "
		"can=%s can_rx=%u can_tx=%u can_nod=%u can_alive=%d can_ident=%d can_err=%u can_state=%u "
		"nvs=%s nvs_opt=%u nvs_used=%u/%u heap_int_min=%u can_push=%u menu=%s menu_sel=%d>>>\n",
		demo::SceneName(),
		tenths / 10,
		tenths % 10,
		static_cast<unsigned>(heap.uFreeInternal),
		static_cast<unsigned>(heap.uFreePsram),
		iRpm,
		iEng,
		iMoving,
		static_cast<unsigned>(app::ModelStackHeadroom()),
		pPort ? app::CanPort::ModeName(pPort->GetMode()) : "off",
		static_cast<unsigned>(pPort ? pPort->GetRxCount() : 0),
		static_cast<unsigned>(pPort ? pPort->GetTxCount() : 0),
		static_cast<unsigned>(pProc ? pProc->GetNodCount() : 0),
		pProc ? pProc->GetAliveUnitCount() : -1,
		pProc ? pProc->GetIdentifiedUnitCount() : -1,
		static_cast<unsigned>(pPort ? pPort->GetErrCount() : 0),
		static_cast<unsigned>(pPort ? pPort->GetBusState() : 0),
		bOpen ? "open" : "off",
		static_cast<unsigned>(app::OptionsLoaded()),
		static_cast<unsigned>(use.uUsed),
		static_cast<unsigned>(use.uTotal),
						// Low-water mark since boot. heap_int is a snapshot taken at
						// a random point in a ThorVG frame and swings by tens of kB;
						// this is the figure that says whether the margin is real.
		static_cast<unsigned>(heap.uMinFreeInternal),
		static_cast<unsigned>(app::ParameterPushCount()),
		menu::LevelName(),
		menu::Selection()
	);
	WriteStr(line);
}

void SaveSettings()
{
	app::Model*		pModel	= app::GetModel();
	app::Settings& settings = app::GetSettings();

	if(pModel == nullptr || settings.IsOpen() == false) {
		WriteStr("<<<SAVE ok=0 written=0>>>\n");
		return;
	}

	// The whole set, not just what is dirty: nothing on this board changes an
	// option by itself, so a dirty-only save would write nothing and prove
	// nothing. Reboot afterwards and check nvs_opt in <<<STATS>>>.
	const uint32_t					uWritten = settings.Save(pModel->GetOptions(), false);
	const app::Settings::Usage use		= settings.GetUsage();

	char line[128];
	std::snprintf(
		line,
		sizeof(line),
		"<<<SAVE ok=%d written=%u used=%u/%u>>>\n",
		uWritten > 0 ? 1 : 0,
		static_cast<unsigned>(uWritten),
		static_cast<unsigned>(use.uUsed),
		static_cast<unsigned>(use.uTotal)
	);
	WriteStr(line);
}

void PushParameter()
{
	app::Model* pModel = app::GetModel();
	if(pModel == nullptr) {
		WriteStr("<<<PUSH ok=0 pushes=0>>>\n");
		return;
	}

	const bool bSent = pModel->SimulateParameterPush();

	// The transfer is asynchronous now: DDS_A posts a burst per 50 ms tick,
	// then the commit, then the apply on the LVGL task. Wait for the services
	// to go idle rather than guessing at a delay.
	for(int i = 0; i < 60 && app::IsPushActive(); ++i)
		platform::SleepMs(50);
	platform::SleepMs(200); // and one more scene tick for the apply

	char line[96];
	std::snprintf(
		line,
		sizeof(line),
		"<<<PUSH ok=%d pushes=%u>>>\n",
		bSent ? 1 : 0,
		static_cast<unsigned>(app::ParameterPushCount())
	);
	WriteStr(line);
}

void Screenshot(int step)
{
	Base64Stream b64;
	int32_t		 w = 0;
	int32_t		 h = 0;

	// On the board the log and the base64 body share one USB endpoint, so any
	// line emitted mid-capture -- by us, by LVGL, by the adapter -- lands in
	// the middle of a base64 line and corrupts the image beyond recovery on
	// the host. Silence logging for the duration; the frame markers below are
	// written directly, so they are unaffected. In the simulator the two are
	// separate streams and this costs nothing.
	platform::SetLogLevel(platform::LogLevel::None);

	char header[96];
	std::snprintf(header, sizeof(header), "<<<SHOT step=%d fmt=rgb888>>>\n", step);
	WriteStr(header);

	const bool ok = demo::CaptureScreenshot(step, SinkToBase64, &b64, &w, &h);
	b64.Finish();

	char footer[96];
	std::snprintf(
		footer, sizeof(footer), "<<<ENDSHOT ok=%d w=%d h=%d>>>\n", ok ? 1 : 0, static_cast<int>(w), static_cast<int>(h)
	);
	WriteStr(footer);

	platform::SetLogLevel(platform::LogLevel::Info);
}

void Help()
{
	WriteStr(
		"<<<HELP h=help i=stats t=toggle m=menu-next M=menu-activate "
		"w=save-settings P=push-param s=shot-half S=shot-full>>>\n"
	);
}

// An arrow key is three bytes on the wire -- Esc [ A..D -- so a byte at a time
// needs somewhere to remember how far through one we are.
//
// A bare Esc is only acted on once the next byte arrives, and that byte is then
// handled as a command in its own right. That is the ambiguity every terminal
// lives with; here it costs nothing, because the only thing Esc does is walk
// the settings page back one level.
enum class KeySeq : uint8_t
{
	None,
	Esc,  // saw Esc, waiting to see whether a '[' follows
	Csi	 // saw Esc [, waiting for the letter
};

constexpr uint8_t ESC = 0x1B;

// The arrow an Esc [ sequence ends in. Returns false for anything else, which
// the caller then treats as an ordinary command byte.
bool ArrowKey(uint8_t c, menu::Key& eKey)
{
	switch(c) {
	case 'A': eKey = menu::Key::Up; return true;
	case 'B': eKey = menu::Key::Down; return true;
	case 'C': eKey = menu::Key::Right; return true;
	case 'D': eKey = menu::Key::Left; return true;
	default:	 return false;
	}
}

void ConsoleTask(void*)
{
	KeySeq eSeq = KeySeq::None;

	WriteStr("<<<CONSOLE ready>>>\n");
	for(;;) {
		uint8_t		 c = 0;
		const size_t n = platform::ConsoleRead(&c, 1);
		if(n != 1) {
			// End of stream. On the board that cannot happen -- the driver
			// blocks forever -- but a simulator run from a pipe ends when the
			// host closes it, and spinning on a dead stream would burn a core.
			platform::SleepMs(50);
			continue;
		}
		if(eSeq == KeySeq::Esc) {
			eSeq = (c == '[') ? KeySeq::Csi : KeySeq::None;
			if(eSeq == KeySeq::Csi)
				continue;
			// Not a sequence after all -- it really was Esc, and c is the next
			// command.
			menu::HandleKey(menu::Key::Esc);
		}
		else if(eSeq == KeySeq::Csi) {
			eSeq = KeySeq::None;
			menu::Key eKey;
			if(ArrowKey(c, eKey)) {
				menu::HandleKey(eKey);
				continue;
			}
			// Some other CSI -- a function key, a mouse report. Let the final
			// byte fall through as a command rather than swallowing it.
		}

		switch(c) {
		case 'h': Help(); break;
		case 'i': PrintStats(); break;
		case 'm': menu::HandleKey(menu::Key::Down); break;
		case 'M': menu::HandleKey(menu::Key::Enter); break;
		case ESC: eSeq = KeySeq::Esc; break;
		case 't':
			demo::ToggleScene();
			PrintStats();
			break;
		case 'w':  SaveSettings(); break;
		case 'P':  PushParameter(); break;
		case 's':  Screenshot(2); break;
		case 'S':  Screenshot(1); break;
		case '\r':
		case '\n': menu::HandleKey(menu::Key::Enter); break;
		default:	  break;
		}
	}
}

} // namespace

namespace demo {

void StartSerialConsole()
{
	if(platform::ConsoleOpen() == false) {
		APP_LOGE(TAG, "no host link; the console will not run");
		return;
	}

	// The stack is large because lv_snapshot_take() draws on the calling
	// thread, and on the board 32 kB of it has to be *contiguous* -- if this
	// ever fails, something mounted or allocated ahead of it fragmented
	// internal RAM. StartTask() reports that itself, which matters: a silent
	// failure here looks exactly like a dead board, with no <<<CONSOLE ready>>>
	// and nothing on the host to say why.
	const platform::TaskConfig cfg{"console", CONSOLE_TASK_STACK, 4};
	platform::StartTask(cfg, ConsoleTask, nullptr);
}

} // namespace demo

#else // DEMO_NO_SERIAL_CONSOLE

namespace demo {
void StartSerialConsole() {}
} // namespace demo

#endif
