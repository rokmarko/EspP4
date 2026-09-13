/**
 * @file ScaleDraw.cpp
 * @brief Bodies of the scale drawing declared in ScaleDraw.h.
 *
 * Every function here is a template over the back end -- `PainterLike auto &P`
 * -- so the bodies would normally have to sit in the header. They do not,
 * because the set of back ends is closed and known: the explicit
 * instantiations at the foot of this file name them, and everything the two
 * entry points call is instantiated along with them. That keeps ThorVG (and Qt)
 * out of every translation unit that merely wants to draw a scale.
 *
 * Adding a back end means adding one pair of instantiations below, and nothing
 * else.
 */

#include "ScaleDraw.h"

#include "PainterTvg.h"
#include "PainterQt.h"

#include <cmath>
#include <cstdio>

namespace scale {

bool Scale::DrawArc(
    PainterLike auto &P,
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

    /* Labels must be queued after the vector work -- see Painter.h. */
    P.Flush();

    P.SetFont(style.GetFont());
    DrawLabels(P, arc, rScale, markings, style.GetOffset().m_fLabel);

    return true;
}

// --------------------------------------------------------------------

void Scale::DrawDash(
    PainterLike auto &P,
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
    PainterLike auto &P,
    const Vec2D &ptC,
    float fAngleRad,
    float fR1,
    float fR2
)
{
    float fSA;
    float fCA;
    common::SinCos(fAngleRad, &fSA, &fCA);

    P.MoveTo(ptC.GetX() + fCA*fR1, ptC.GetY() - fSA*fR1);
    P.LineTo(ptC.GetX() + fCA*fR2, ptC.GetY() - fSA*fR2);
}

// --------------------------------------------------------------------

void Scale::DrawLabel(
    PainterLike auto &P,
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
    PainterLike auto &P,
    const Vec2D &ptC,
    float fAngleRad,
    float fR,
    const char *pszText,
    bool bInside
)
{
    const Size2D size = P.TextSize(pszText);

    float fSA;
    float fCA;
    common::SinCos(fAngleRad, &fSA, &fCA);

    /* Reference point: centre of the text box, sitting on the arc. */
    float fCX = ptC.GetX() + fCA*fR;
    const float fCY = ptC.GetY() - fSA*fR;

    /* Wide labels near 3 and 9 o'clock would otherwise lean into the scale. */
    const float fCorr = (size.fW - size.fH)/2.0f * fCA;
    fCX += bInside ? -fCorr : fCorr;

    P.DrawTextCentred(fCX, fCY, pszText);
}

// --------------------------------------------------------------------

void Scale::DrawBands(
    PainterLike auto &P,
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

        P.SetPen(colors.GetColor(color), style.GetBand().m_fWidth);
        DrawBand(P, arc, range, bands.GetRange(), fR);
        P.Emit();
    }
}

// --------------------------------------------------------------------

void Scale::DrawMinorDashes(
    PainterLike auto &P,
    const Arc2D &arc,
    const RangeF &range,
    ::gui::ARGB col,
    const style::Dash &dashMinor,
    float fOffset,
    float fMinorStep
)
{
    if (fMinorStep <= 0) return;

    P.SetPen(col, dashMinor.m_fWidth);

    // Minor dashes are drawn using real because the major step
    // may not be a multiplier of minor count.
    const auto rInside = range.GetInside(fMinorStep);

    for (float f=rInside.GetLow(), fEnd=rInside.GetHigh()+fMinorStep/4; f<fEnd; f+=fMinorStep)
        DrawDash(P, f, arc, range, dashMinor.m_fLength, fOffset);

    P.Emit();
}

// --------------------------------------------------------------------

void Scale::DrawMajorDashes(
    PainterLike auto &P,
    const Arc2D &arc,
    const RangeF &range,
    ::gui::ARGB col,
    const style::Dash &dashMajor,
    float fOffset,
    float fMajorStep
)
{
    if (fMajorStep <= 0) return;

    P.SetPen(col, dashMajor.m_fWidth);

    const auto rInside = range.GetInside(fMajorStep);

    for (float f=rInside.GetLow(), fEnd=rInside.GetHigh()+fMajorStep/4; f<fEnd; f+=fMajorStep)
        DrawDash(P, f, arc, range, dashMajor.m_fLength, fOffset);

    P.Emit();
}

// --------------------------------------------------------------------

void Scale::DrawLabels(
    PainterLike auto &P,
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
    PainterLike auto &P,
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
    PainterLike auto &P,
    const Vec2D &ptC,
    float fR,
    float fStartDeg,
    float fSpanDeg
)
{
    P.AppendArc(ptC.GetX(), ptC.GetY(), fR, fStartDeg, fSpanDeg);
}

// --------------------------------------------------------------------
//  IAS: two scales, coloured arcs, white flap band, V-speed markings
// --------------------------------------------------------------------

std::pair<Arc2D, Arc2D> Scale::DrawArcIAS(
    PainterLike auto &P,
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

    /* Queue the vector work before the labels. */
    P.Flush();
    DrawLabels(P, a2, rScale, markings, style.GetOffset().m_fLabel);

    /* V-markings go on top of the labels, so they are a batch of their own. */
    DrawVMarkings(P, a2, style, styleIAS, bands.GetRange(), markingsIAS.GetVMarkings());
    P.Flush();

    return {a1, a2};
}

// --------------------------------------------------------------------

/* Will not draw the final red band -- DrawRedDashesIAS() marks it instead. */
void Scale::DrawBandsIAS(
    PainterLike auto &P,
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

        P.SetPen(colors.GetColor(color), style.GetBand().m_fWidth);
        DrawBand(P, arc, range, bands.GetRange(), fR);
        P.Emit();
    }
}

// --------------------------------------------------------------------

void Scale::DrawWhiteBand(
    PainterLike auto &P,
    const Arc2D &arc,
    const RangeF &whiteRange,
    const RangeF &range,
    const style::Band &bandWhite,
    float fOffset
)
{
    if (whiteRange.IsValidNonEmpty() == false) return;

    P.SetPen(C32_WHITE, bandWhite.m_fWidth);
    DrawBand(P, arc, whiteRange, range, arc.GetRadius() + fOffset);
    P.Emit();
}

// --------------------------------------------------------------------

void Scale::DrawRedDashesIAS(
    PainterLike auto &P,
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
        P.SetPen(colors.GetColor(parameter::Color::Red), redDash.m_fWidth);
        DrawDash(P, range.GetLow(), arc, bands.GetRange(), redDash.m_fLength, fOffset);
        P.Emit();
    }
}

// --------------------------------------------------------------------

void Scale::DrawVMarkings(
    PainterLike auto &P,
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
    PainterLike auto &P,
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
        PRINTF("Unknown VMark shape %d\n", static_cast<int>(mark.m_eShape));
    }
}

// --------------------------------------------------------------------

void Scale::DrawTriangle(
    PainterLike auto &P,
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
    PainterLike auto &P,
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

    P.PushTransform(ptC.GetX() + fCA*fRadius, ptC.GetY() - fSA*fRadius,
                    common::Deg(fAngleRad));

    /* Built in local space with +x pointing radially outward: apex inward,
     * base sitting on the arc. */
    P.MoveTo(0.0f, -fLength);
    P.LineTo(-fLength*3/2, 0.0f);
    P.LineTo(0.0f, fLength);
    P.ClosePath();

    if (bDot)
        P.AppendCircle(fLength*1.2f, 0.0f, fLength*0.3f);

    /* Inside the bracket: ThorVG captures the matrix as the path is taken. */
    P.Emit();
    P.PopTransform();
}

// --------------------------------------------------------------------
//  Back ends
//
//  Instantiating the two entry points pulls in every helper they reach, so
//  these four lines are the whole list. PainterQt.h is empty without Qt, hence
//  the guard.
// --------------------------------------------------------------------

template bool Scale::DrawArc<PainterTvg>(PainterTvg &, const Arc2D &, const Markings &,
    const parameter::Bands &, const parameter::gui::Colors &, const style::Style &);

template std::pair<Arc2D, Arc2D> Scale::DrawArcIAS<PainterTvg>(PainterTvg &, const Arc2D &,
    const Markings &, const MarkingsIAS &, const parameter::Bands &,
    const parameter::gui::Colors &, const style::Style &, const style::StyleIAS &);

#if defined(QT_CORE_LIB)

template bool Scale::DrawArc<PainterQt>(PainterQt &, const Arc2D &, const Markings &,
    const parameter::Bands &, const parameter::gui::Colors &, const style::Style &);

template std::pair<Arc2D, Arc2D> Scale::DrawArcIAS<PainterQt>(PainterQt &, const Arc2D &,
    const Markings &, const MarkingsIAS &, const parameter::Bands &,
    const parameter::gui::Colors &, const style::Style &, const style::StyleIAS &);

#endif

} // namespace scale
