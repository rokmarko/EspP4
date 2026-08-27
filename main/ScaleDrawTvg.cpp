/**
 * @file ScaleDrawTvg.cpp
 * @brief ThorVG implementation of Common/Scale/ScaleDrawQt.cpp.
 *
 * The structure deliberately mirrors the Qt original, function for function,
 * so the two stay easy to diff. Three things differ, and all three come from
 * the back end rather than from the scale itself:
 *
 *  - Strokes that share a pen are collected into one path and stroked once.
 *    ThorVG costs ~100 ms per frame on this board; paths are the currency.
 *  - Bands are arcs appended to a path and stroked, not QPainter::drawArc.
 *  - Labels go through lv_draw_label. ThorVG in LVGL exposes no text API, so
 *    "draw the glyphs as a path" is not available.
 */

#include "ScaleDrawTvg.h"

#include "Parameter/ParamBands.h"

#include "KanardiaFont.h"

#include "esp_log.h"

#include <cmath>
#include <cstdio>

namespace scale {
namespace tvg {

namespace {

constexpr const char *TAG = "scale";

/** Arc2D angles are counter-clockwise; LVGL's vector API is clockwise. */
constexpr float ToLvglDeg(float fArc2DDeg) { return -fArc2DDeg; }

} // namespace

// --------------------------------------------------------------------
//  Painter
// --------------------------------------------------------------------

void Painter::SetPen(::gui::ARGB argb, float fWidth)
{
    m_penColor = argb;

    m_dsc.set_fill_opa(LV_OPA_TRANSP);
    m_dsc.set_stroke_color(ToColor32(argb));
    m_dsc.set_stroke_opa(LV_OPA_COVER);
    m_dsc.set_stroke_width(fWidth);
    m_dsc.set_stroke_cap(LV_VECTOR_STROKE_CAP_BUTT);
    m_dsc.set_stroke_join(LV_VECTOR_STROKE_JOIN_MITER);
}

// --------------------------------------------------------------------

void Painter::SetBrush(::gui::ARGB argb)
{
    m_dsc.set_stroke_opa(LV_OPA_TRANSP);
    m_dsc.set_fill_color(ToColor32(argb));
    m_dsc.set_fill_opa(LV_OPA_COVER);
    m_dsc.set_fill_rule(LV_VECTOR_FILL_NONZERO);
}

// --------------------------------------------------------------------

void Painter::Emit()
{
    m_dsc.add_path(m_path);
    m_path.clear();
}

// --------------------------------------------------------------------

const lv_font_t *Painter::CreateFont(const style::Font &font)
{
    /* Only the sizes CMake generated exist; pick the closest one. The table
     * comes straight out of the generated header, so it follows
     * KANARDIA_FONT_SIZES without anyone having to remember to update it. */
    struct Entry { int iSize; const lv_font_t *pFont; };
    static const Entry aFonts[] = {
#define KANARDIA_FONT_ENTRY(px, sym) { px, &sym },
        KANARDIA_FONT_LIST(KANARDIA_FONT_ENTRY)
#undef KANARDIA_FONT_ENTRY
    };

    const lv_font_t *pBest = aFonts[0].pFont;
    int iBestDiff = std::abs(font.m_iSize - aFonts[0].iSize);
    for (const auto &e : aFonts) {
        const int iDiff = std::abs(font.m_iSize - e.iSize);
        if (iDiff < iBestDiff) {
            iBestDiff = iDiff;
            pBest = e.pFont;
        }
    }
    return pBest;
}

// --------------------------------------------------------------------
//  Scale
// --------------------------------------------------------------------

bool Scale::DrawArc(
    Painter &P,
    const Arc2D &arc,
    const Markings &markings,
    const parameter::Bands &bands,          // Must be in user units!
    const parameter::gui::Colors &colors,
    const style::Style &style
)
{
    const auto rScale = ConvertToScale(bands.GetRange(), markings.GetMultiples());
    if (rScale.IsValidNonEmpty() == false)
        return false;

    const ::gui::ARGB col = colors.GetColor(parameter::Color::White);
    DrawMinorDashes(P, arc, rScale, col, style.GetMinor(), style.GetOffset().m_fMinorDash, markings.GetMinorStep());
    DrawBands(P, arc, style, bands, colors);
    DrawMajorDashes(P, arc, rScale, col, style.GetMajor(), style.GetOffset().m_fMajorDash, markings.GetMajorStep());

    /* Labels are LVGL draw tasks and the layer runs its tasks in the order they
     * were added, so the vector work has to be queued before them. */
    P.Flush();

    P.SetFont(style.GetFont());
    DrawLabels(P, arc, rScale, markings, style.GetOffset().m_fLabel);

    return true;
}

// --------------------------------------------------------------------

void Scale::DrawDash(
    Painter &P,
    float fX,
    const Arc2D &arc,
    const RangeF &range,
    float fDashLength,
    float fOffset
)
{
    const auto angle = arc.GetAngle(range.GetRelative(fX));
    const auto r1 = arc.GetRadius() + fOffset;
    const auto r2 = r1 + fDashLength;

    DrawDash(P, arc.GetCenter(), angle, r1, r2);
}

// --------------------------------------------------------------------

void Scale::DrawDash(
    Painter &P,
    const Vec2D &ptC,
    float fAngleRad,
    float fR1,
    float fR2
)
{
    float fSA;
    float fCA;
    common::SinCos(fAngleRad, &fSA, &fCA);

    auto &path = P.GetPath();
    path.move_to(ptC.GetX() + fCA*fR1, ptC.GetY() - fSA*fR1);
    path.line_to(ptC.GetX() + fCA*fR2, ptC.GetY() - fSA*fR2);
}

// --------------------------------------------------------------------

void Scale::DrawLabel(
    Painter &P,
    float fX,
    const Arc2D &arc,
    const RangeF &range,
    const char *pszText,
    float fOffset
)
{
    const auto angle = arc.GetAngle(range.GetRelative(fX));
    DrawLabelAt(
        P, arc.GetCenter(),
        angle,
        arc.GetRadius()+fOffset,
        pszText, fOffset < 0
    );
}

// --------------------------------------------------------------------

void Scale::DrawLabelAt(
    Painter &P,
    const Vec2D &ptC,
    float fAngleRad,
    float fR,
    const char *pszText,
    bool bInside
)
{
    const lv_font_t *pFont = P.GetFont();
    if (pFont == nullptr) return;

    lv_point_t size{};
    lv_text_get_size(&size, pszText, pFont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

    float fSA;
    float fCA;
    common::SinCos(fAngleRad, &fSA, &fCA);

    /* Reference point: centre of the text box, sitting on the arc. */
    float fCX = ptC.GetX() + fCA*fR;
    const float fCY = ptC.GetY() - fSA*fR;

    /* Wide labels near 3 and 9 o'clock would otherwise lean into the scale;
     * the Qt version applies the same radial correction. */
    const float fCorr = (size.x - size.y)/2.0f * fCA;
    fCX += bInside ? -fCorr : fCorr;

    lv_area_t area;
    area.x1 = static_cast<int32_t>(std::lround(fCX - size.x/2.0f));
    area.y1 = static_cast<int32_t>(std::lround(fCY - size.y/2.0f));
    area.x2 = area.x1 + size.x - 1;
    area.y2 = area.y1 + size.y - 1;

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text       = pszText;
    dsc.text_local = 1;      /* pszText is a stack buffer; make LVGL copy it */
    dsc.font       = pFont;
    /* Always white, whatever the pen is: DrawTextAsPath() in the Qt version
     * forces white too, which is why labels survive DrawRedDashesIAS() leaving
     * a red pen behind. */
    dsc.color      = lv_color_hex(C32_WHITE);
    dsc.opa        = LV_OPA_COVER;
    dsc.align      = LV_TEXT_ALIGN_CENTER;

    lv_draw_label(P.GetLayer(), &dsc, &area);
}

// --------------------------------------------------------------------

void Scale::DrawBands(
    Painter &P,
    const Arc2D &arc,
    const style::Style &style,
    const parameter::Bands &bands,
    const parameter::gui::Colors &colors
)
{
    // Loop over bands
    const float fR = arc.GetRadius() + style.GetOffset().m_fBand;
    for (int i=0; i<bands.GetCount(); ++i) {
        auto range = bands.GetRange(i);
        auto color = bands.GetColor(i);
        if (color == parameter::Color::NoColor)
            continue;

        P.SetPen(colors.GetColor(color), style.GetBand());
        DrawBand(P, arc, range, bands.GetRange(), fR);
        P.Emit();
    }
}

// --------------------------------------------------------------------

void Scale::DrawMinorDashes(
    Painter &P,
    const Arc2D &arc,
    const RangeF &range,
    ::gui::ARGB col,
    const style::Dash &dashMinor,
    float fOffset,
    float fMinorStep
)
{
    if (fMinorStep <= 0) return;

    P.SetPen(col, dashMinor);

    // Minor dashes are drawn using real because the major step
    // may not be a multiplier of minor count.
    const auto rInside = range.GetInside(fMinorStep);

    for (float f=rInside.GetLow(), fEnd=rInside.GetHigh()+fMinorStep/4; f<fEnd; f+=fMinorStep)
        DrawDash(P, f, arc, range, dashMinor.m_fLength, fOffset);

    P.Emit();
}

// --------------------------------------------------------------------

void Scale::DrawMajorDashes(
    Painter &P,
    const Arc2D &arc,
    const RangeF &range,
    ::gui::ARGB col,
    const style::Dash &dashMajor,
    float fOffset,
    float fMajorStep
)
{
    if (fMajorStep <= 0) return;

    P.SetPen(col, dashMajor);

    const auto rInside = range.GetInside(fMajorStep);

    for (float f=rInside.GetLow(), fEnd=rInside.GetHigh()+fMajorStep/4; f<fEnd; f+=fMajorStep)
        DrawDash(P, f, arc, range, dashMajor.m_fLength, fOffset);

    P.Emit();
}

// --------------------------------------------------------------------

void Scale::DrawLabels(
    Painter &P,
    const Arc2D &arc,
    const RangeF &range,
    const Markings &markings,
    float fOffset
)
{
    const auto fStep = markings.GetLabelStep();
    const auto iDec  = markings.GetDecimals();
    if (fStep <= 0.0f) return;

    const float fEnd = [&]() {
        float f = range.GetHigh();
        // if scale arc is full circle, omit the last label.
        if (arc.IsCircle())
            f -= 3*fStep/4;
        return f;
    }();

    const auto fEps = fStep/20.0f;
    const RangeF rCheck(markings.GetLabelStart(), fEnd);
    for (float f=rCheck.GetLow(); f<fEnd+fEps; f+=fStep) {
        const auto fs = common::Snap(f, fStep);
        if (rCheck.Contains(fs, -fEps)) {
            char szText[24];
            std::snprintf(szText, sizeof(szText), "%.*f", iDec, static_cast<double>(fs));
            DrawLabel(P, fs, arc, range, szText, fOffset);
        }
    }
}

// --------------------------------------------------------------------

void Scale::DrawBand(
    Painter &P,
    const Arc2D &arc,
    const RangeF &rangeBand,
    const RangeF &rangeScale,
    float fR
)
{
    const auto fSaDeg = common::Deg(arc.GetStartAngle());
    const auto fSpDeg = common::Deg(arc.GetSpan());

    const float fStart = GetAngleDeg(rangeBand.GetLow(), fSaDeg, fSpDeg, rangeScale);
    const float fSpan  = GetAngleDeg(rangeBand.GetHigh(), fSaDeg, fSpDeg, rangeScale) - fStart;

    DrawBand(P, arc.GetCenter(), fR, fStart, fSpan);
}

// --------------------------------------------------------------------

void Scale::DrawBand(
    Painter &P,
    const Vec2D &ptC,
    float fR,
    float fStartDeg,
    float fSpanDeg
)
{
    P.GetPath().append_arc(ptC.GetX(), ptC.GetY(), fR,
                           ToLvglDeg(fStartDeg), ToLvglDeg(fSpanDeg), false);
}

// --------------------------------------------------------------------
//  IAS: two scales, coloured arcs, white flap band, V-speed markings
// --------------------------------------------------------------------

std::pair<Arc2D, Arc2D> Scale::DrawArcIAS(
    Painter &P,
    const Arc2D &arc,
    const Markings &markings,
    const MarkingsIAS &markingsIAS,
    const parameter::Bands &bands,
    const parameter::gui::Colors &colors,
    const style::Style &style,
    const style::StyleIAS &styleIAS
)
{
    Arc2D a1 = arc;
    Arc2D a2 = arc;
    P.SetFont(style.GetFont());
    const ::gui::ARGB col = colors.GetColor(parameter::Color::White);

    /* Pre-scale. An IAS scale usually starts at some positive value, say
     * 60 km/h; the first 20 degrees carry a few radial dashes down to zero. */
    if (bands.GetLow() > 0.0f) {
        constexpr float fSpan1 = -RAD_20;
        a1.SetSpan(fSpan1);
        const auto r1 = ConvertToScale(RangeF(0.0f, bands.GetLow()), markings.GetMultiples());

        /* Fixed style: one minor dash between zero and the starting value. */
        const int iMaj = static_cast<int>(r1.GetHigh());
        const int iMin = iMaj/2;
        DrawMinorDashes(P, a1, r1, col, style.GetMinor(), style.GetOffset().m_fMinorDash, iMin);
        DrawMajorDashes(P, a1, r1, col, style.GetMajor(), style.GetOffset().m_fMajorDash, iMaj);

        /* Label zero. Multiples and decimals match the second scale. */
        P.Flush();
        const float f = r1.GetLow();
        char szText[24];
        std::snprintf(szText, sizeof(szText), "%.*f", markings.GetDecimals(), static_cast<double>(f));
        DrawLabel(P, f, a1, r1, szText, style.GetOffset().m_fLabel);

        a2.SetStartAngle(arc.GetStartAngle() + fSpan1);
        a2.SetSpan(arc.GetSpan() - fSpan1);
    }
    else {
        a1.SetSpan(0.0f);
    }

    /* Scale. Dashes and labels work in scale units, bands in user units. */
    const auto rScale = ConvertToScale(bands.GetRange(), markings.GetMultiples());

    DrawMinorDashes(P, a2, rScale, col, style.GetMinor(), style.GetOffset().m_fMinorDash, markings.GetMinorStep());
    DrawBandsIAS(P, a2, style, bands, colors);
    DrawWhiteBand(P, a2, markingsIAS.GetWhiteRange(), bands.GetRange(),
                  styleIAS.GetWhiteBand(), styleIAS.GetOffset().m_fWhiteBand);

    DrawMajorDashes(P, a2, rScale, col, style.GetMajor(), style.GetOffset().m_fMajorDash, markings.GetMajorStep());
    DrawRedDashesIAS(P, a2, bands, colors, styleIAS.GetRedDash(), styleIAS.GetOffset().m_fRedDash);

    /* Labels are LVGL tasks; queue the vector work before them. */
    P.Flush();
    DrawLabels(P, a2, rScale, markings, style.GetOffset().m_fLabel);

    /* V-markings go on top of the labels, so they are a task of their own. */
    DrawVMarkings(P, a2, style, styleIAS, bands.GetRange(), markingsIAS.GetVMarkings());
    P.Flush();

    return {a1, a2};
}

// --------------------------------------------------------------------

/* Will not draw the final red band -- DrawRedDashesIAS() marks it instead. */
void Scale::DrawBandsIAS(
    Painter &P,
    const Arc2D &arc,
    const style::Style &style,
    const parameter::Bands &bands,
    const parameter::gui::Colors &colors
)
{
    const float fR = arc.GetRadius() + style.GetOffset().m_fBand;
    for (int i=0; i<bands.GetCount(); ++i) {
        auto range = bands.GetRange(i);
        auto color = bands.GetColor(i);
        if (color == parameter::Color::NoColor || color == parameter::Color::Red)
            continue;

        P.SetPen(colors.GetColor(color), style.GetBand());
        DrawBand(P, arc, range, bands.GetRange(), fR);
        P.Emit();
    }
}

// --------------------------------------------------------------------

void Scale::DrawWhiteBand(
    Painter &P,
    const Arc2D &arc,
    const RangeF &whiteRange,
    const RangeF &range,
    const style::Band &bandWhite,
    float fOffset
)
{
    if (whiteRange.IsValidNonEmpty() == false) return;

    P.SetPen(C32_WHITE, bandWhite);
    DrawBand(P, arc, whiteRange, range, arc.GetRadius() + fOffset);
    P.Emit();
}

// --------------------------------------------------------------------

void Scale::DrawRedDashesIAS(
    Painter &P,
    const Arc2D &arc,
    const parameter::Bands &bands,
    const parameter::gui::Colors &colors,
    const style::Dash &redDash,
    float fOffset
)
{
    for (int i=0; i<bands.GetCount(); ++i) {
        if (bands.GetColor(i) != parameter::Color::Red)
            continue;

        const auto range = bands.GetRange(i);
        P.SetPen(colors.GetColor(parameter::Color::Red), redDash);
        DrawDash(P, range.GetLow(), arc, bands.GetRange(), redDash.m_fLength, fOffset);
        P.Emit();
    }
}

// --------------------------------------------------------------------

void Scale::DrawVMarkings(
    Painter &P,
    const Arc2D &arc,
    const style::Style &style,
    const style::StyleIAS &styleIAS,
    const RangeF &range,
    const std::vector<VMark> &vMarks
)
{
    for (const auto &mark : vMarks) {
        if (range.Contains(mark.m_fVal))
            DrawVMark(P, arc, style, styleIAS, range, mark);
    }
}

// --------------------------------------------------------------------

void Scale::DrawVMark(
    Painter &P,
    const Arc2D &arc,
    const style::Style &style [[maybe_unused]],
    const style::StyleIAS &styleIAS,
    const RangeF &range,
    const VMark &mark
)
{
    if (IsTriangle(mark.m_eShape)) {
        const auto &vStyle = styleIAS.GetVTriangle();
        P.SetBrush(mark.m_color);
        DrawTriangle(P, mark.m_fVal, arc, range,
                     vStyle.m_fLength, styleIAS.GetOffset().m_fVTriangle,
                     mark.m_eShape == Shape::TriangleDot);
    }
    else if (mark.m_eShape == Shape::Dash) {
        const auto &vStyle = styleIAS.GetVDash();
        P.SetPen(mark.m_color, vStyle.m_fWidth);
        DrawDash(P, mark.m_fVal, arc, range, vStyle.m_fLength, styleIAS.GetOffset().m_fVDash);
        P.Emit();
    }
    else if (mark.m_eShape == Shape::DashDash) {
        /* Coloured dash with a white segment laid over its outer third. */
        const auto &vStyle = styleIAS.GetVDash();
        const float fLen = vStyle.m_fLength;

        P.SetPen(mark.m_color, vStyle.m_fWidth);
        DrawDash(P, mark.m_fVal, arc, range, fLen, styleIAS.GetOffset().m_fVDash);
        P.Emit();

        P.SetPen(C32_WHITE, vStyle.m_fWidth);
        DrawDash(P, mark.m_fVal, arc, range, fLen/3.0f, styleIAS.GetOffset().m_fVDash + fLen/3.0f);
        P.Emit();
    }
    else {
        ESP_LOGW(TAG, "unknown VMark shape %d", static_cast<int>(mark.m_eShape));
    }
}

// --------------------------------------------------------------------

void Scale::DrawTriangle(
    Painter &P,
    float fX,
    const Arc2D &arc,
    const RangeF &range,
    float fSideLength,
    float fOffset,
    bool bDot
)
{
    const auto angle = arc.GetAngle(range.GetRelative(fX));
    DrawTriangle(P, arc.GetCenter(), angle, arc.GetRadius() + fOffset, fSideLength, bDot);
}

// --------------------------------------------------------------------

void Scale::DrawTriangle(
    Painter &P,
    const Vec2D &ptC,
    float fAngleRad,
    float fRadius,
    float fLength,
    bool bDot
)
{
    float fSA;
    float fCA;
    common::SinCos(fAngleRad, &fSA, &fCA);

    auto &dsc  = P.GetDraw();
    auto &path = P.GetPath();

    /* Built in local space with +x pointing radially outward, exactly as the Qt
     * version does: apex inward, base sitting on the arc. */
    path.move_to(0.0f, -fLength);
    path.line_to(-fLength*3/2, 0.0f);
    path.line_to(0.0f, fLength);
    path.close();

    if (bDot) {
        const float fR = fLength*0.3f;
        path.append_circle(fLength*1.2f, 0.0f, fR, fR);
    }

    dsc.identity();
    dsc.translate(ptC.GetX() + fCA*fRadius, ptC.GetY() - fSA*fRadius);
    dsc.rotate(-common::Deg(fAngleRad));
    P.Emit();
    dsc.identity();
}

}} // namespace scale::tvg
