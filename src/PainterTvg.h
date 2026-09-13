/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// ThorVG/LVGL back end for the PainterLike concept.
//
// Paths are appended to an lv_draw_vector_dsc_t, which ThorVG rasterises inside
// LVGL; labels go to lv_draw_label, because ThorVG exposes no text API here.
// Those are two different pipelines running off one layer in task order, which
// is what Flush() exists to sequence.
//
// Angles arrive in Arc2D sense -- counter-clockwise -- and LVGL's vector API
// measures clockwise, so every angle handed to it is negated.

#include "KanardiaCommon.h"

#include "Painter.h"

#include "lvgl.h"
#include "lvgl_cpp.h"

namespace scale {

// ARGB as used all over Common -> lv_color32_t, which is laid out b,g,r,a.
//
// The alpha byte is dropped on purpose. Common's colour constants carry no
// alpha (C32_WHITE is plain 0xffffff), and the Qt back end converts them with
// QColor(QRgb), which is opaque by definition -- so opaque is what a scale
// colour has always meant. Honouring the byte instead renders every white dash
// and label at opacity zero.
constexpr lv_color32_t ToColor32(::gui::ARGB c)
{
	return lv_color32_t{
		static_cast<uint8_t>(::gui::GetBlue(c)),
		static_cast<uint8_t>(::gui::GetGreen(c)),
		static_cast<uint8_t>(::gui::GetRed(c)),
		0xFF,
	};
}

// The drawing target plus the current pen, brush and font.
//
// It owns the layer's vector descriptor and one scratch path that every
// primitive reuses. The descriptor is where the pen and brush actually live --
// this object only caches the font, which lv_draw_label needs by pointer.
//
// Move-only, because lvgl::VectorDraw and lvgl::VectorPath own their C objects.
// One is built per frame on the LVGL task's stack, between init_layer() and
// finish_layer().
class PainterTvg
{
public:
	explicit PainterTvg(lv_layer_t* pLayer) :
		m_pLayer(pLayer),
		m_dsc(pLayer)
	{}

	PainterTvg(const PainterTvg&)				  = delete;
	PainterTvg& operator=(const PainterTvg&) = delete;

	// -- PainterLike ------------------------------------------------------

	// Stroke argb at fWidth with butt caps and no fill -- Qt's flat pen.
	void SetPen(::gui::ARGB argb, float fWidth);

	// Fill with argb and stroke nothing -- Qt's brush with a null pen.
	void SetBrush(::gui::ARGB argb);

	// Pick the built-in LVGL font closest to font.
	void SetFont(const style::Font& font) { m_pFont = CreateFont(font); }

	void MoveTo(float fX, float fY) { m_path.move_to(fX, fY); }
	void LineTo(float fX, float fY) { m_path.line_to(fX, fY); }
	void ClosePath() { m_path.close(); }

	void AppendArc(float fCX, float fCY, float fR, float fStartDeg, float fSpanDeg)
	{
		m_path.append_arc(fCX, fCY, fR, -fStartDeg, -fSpanDeg, false);
	}

	void AppendCircle(float fCX, float fCY, float fR) { m_path.append_circle(fCX, fCY, fR, fR); }

	// There is no transform stack in the descriptor, so the pop resets to
	// identity. That is only equivalent to a pop while nothing nests, which the
	// concept requires.
	void PushTransform(float fTX, float fTY, float fRotDeg)
	{
		m_dsc.identity();
		m_dsc.translate(fTX, fTY);
		m_dsc.rotate(-fRotDeg);
	}

	void PopTransform() { m_dsc.identity(); }

	Size2D TextSize(const char* pszText) const;
	void	 DrawTextCentred(float fCX, float fCY, const char* pszText);

	// Hand the scratch path to the descriptor with whatever pen or brush is
	// currently set, and clear it. Nothing reaches ThorVG until Flush().
	void Emit();

	// Queue everything accumulated so far as one vector draw task.
	void Flush() { m_dsc.draw(); }

	// -- Beyond the concept -----------------------------------------------

	// VectorScene draws its faces, needles and gradients straight against the
	// LVGL objects. None of that has a Qt counterpart in this project, so it
	// stays here rather than widening the interface.
	lv_layer_t*			GetLayer() { return m_pLayer; }
	lvgl::VectorDraw& GetDraw() { return m_dsc; }
	lvgl::VectorPath& GetPath() { return m_path; }

	// Nearest generated Kanardia face to font.
	//
	// The family name and the bold flag are ignored -- there is no font engine
	// on the board, only the fixed set of sizes main/CMakeLists.txt runs
	// through lv_font_conv.
	static const lv_font_t* CreateFont(const style::Font& font);

	const lv_font_t* GetFont() const { return m_pFont; }

private:
	lv_layer_t*		  m_pLayer;
	lvgl::VectorDraw m_dsc;
	lvgl::VectorPath m_path{LV_VECTOR_PATH_QUALITY_HIGH};
	const lv_font_t* m_pFont = nullptr;
};

static_assert(PainterLike<PainterTvg>);

} // namespace scale
