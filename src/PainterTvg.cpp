/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// Out-of-line members of the ThorVG back end.

#include "PainterTvg.h"

#include "KanardiaFont.h"

#include <cmath>

namespace scale {

// --------------------------------------------------------------------

void PainterTvg::SetPen(::gui::ARGB argb, float fWidth)
{
	m_dsc.set_fill_opa(LV_OPA_TRANSP);
	m_dsc.set_stroke_color(ToColor32(argb));
	m_dsc.set_stroke_opa(LV_OPA_COVER);
	m_dsc.set_stroke_width(fWidth);
	m_dsc.set_stroke_cap(LV_VECTOR_STROKE_CAP_BUTT);
	m_dsc.set_stroke_join(LV_VECTOR_STROKE_JOIN_MITER);
}

// --------------------------------------------------------------------

void PainterTvg::SetBrush(::gui::ARGB argb)
{
	m_dsc.set_stroke_opa(LV_OPA_TRANSP);
	m_dsc.set_fill_color(ToColor32(argb));
	m_dsc.set_fill_opa(LV_OPA_COVER);
	m_dsc.set_fill_rule(LV_VECTOR_FILL_NONZERO);
}

// --------------------------------------------------------------------

void PainterTvg::Emit()
{
	m_dsc.add_path(m_path);
	m_path.clear();
}

// --------------------------------------------------------------------

Size2D PainterTvg::TextSize(const char* pszText) const
{
	if(m_pFont == nullptr)
		return {0.0f, 0.0f};

	lv_point_t size{};
	lv_text_get_size(&size, pszText, m_pFont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
	return {static_cast<float>(size.x), static_cast<float>(size.y)};
}

// --------------------------------------------------------------------

void PainterTvg::DrawTextAnchored(float fX, float fY, const char* pszText, float fAX, float fAY, ::gui::ARGB argb)
{
	if(m_pFont == nullptr)
		return;

	const Size2D size = TextSize(pszText);

	lv_area_t area;
	area.x1 = static_cast<int32_t>(std::lround(fX - fAX * size.fW));
	area.y1 = static_cast<int32_t>(std::lround(fY - fAY * size.fH));
	area.x2 = area.x1 + static_cast<int32_t>(size.fW) - 1;
	area.y2 = area.y1 + static_cast<int32_t>(size.fH) - 1;

	lv_draw_label_dsc_t dsc;
	lv_draw_label_dsc_init(&dsc);
	dsc.text			= pszText;
	dsc.text_local = 1;	  // pszText is a stack buffer; make LVGL copy it
	dsc.font			= m_pFont;
	// Never the pen: the Qt back end forces its own colour too, which is why
	// labels survive a red V-speed dash leaving its pen behind. The scale
	// takes the default white; a panel item's readout names its band colour.
	dsc.color = lv_color_hex(argb);
	dsc.opa	 = LV_OPA_COVER;
	dsc.align = LV_TEXT_ALIGN_CENTER;

	lv_draw_label(m_pLayer, &dsc, &area);
}

// --------------------------------------------------------------------

const lv_font_t* PainterTvg::CreateFont(const style::Font& font)
{
	// Only the sizes CMake generated exist; pick the closest one. The table
	// comes straight out of the generated header, so it follows
	// KANARDIA_FONT_SIZES without anyone having to remember to update it.
	struct Entry
	{
		int				  iSize;
		const lv_font_t* pFont;
	};
	static const Entry aFonts[] = {
#define KANARDIA_FONT_ENTRY(px, sym) {px, &sym},
		KANARDIA_FONT_LIST(KANARDIA_FONT_ENTRY)
#undef KANARDIA_FONT_ENTRY
	};

	const lv_font_t* pBest		= aFonts[0].pFont;
	int				  iBestDiff = std::abs(font.m_iSize - aFonts[0].iSize);
	for(const auto& e : aFonts) {
		const int iDiff = std::abs(font.m_iSize - e.iSize);
		if(iDiff < iBestDiff) {
			iBestDiff = iDiff;
			pBest		 = e.pFont;
		}
	}
	return pBest;
}

// --------------------------------------------------------------------

} // namespace scale
