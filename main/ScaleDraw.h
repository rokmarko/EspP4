#pragma once

/**
 * @file ScaleDraw.h
 * @brief The scale, drawn once, against any PainterLike back end.
 *
 * This is the shared body that ScaleDrawQt.cpp and ScaleDrawTvg.cpp used to
 * carry a copy of each: dash stepping, label snapping, band angles, the IAS
 * pre-scale and the V-speed markings. Nothing here knows what it is drawing
 * into -- every primitive goes through the Painter concept, so the generated
 * code matches a hand-written back end.
 *
 * The class is not a template; each drawing call is. The back end arrives as a
 * `PainterLike auto &P`, so a caller names no template argument and the two
 * back ends never appear in this header. The bodies live in ScaleDraw.cpp,
 * which is also the one place that says which back ends this build
 * instantiates -- see the explicit instantiations at the foot of that file.
 *
 * Angles follow Arc2D throughout: radians internally, degrees at the Painter
 * boundary, zero at 3 o'clock, growing counter-clockwise on screen.
 *
 * Pens and paths batch. Strokes sharing a colour and width are accumulated and
 * emitted together, and Flush() separates vector work from the labels that must
 * land on top of it. Both matter on the panel and cost nothing on a desktop --
 * see Painter.h.
 */

#include "KanardiaCommon.h"

#include "Painter.h"

#include "Defines.h"
#include "Geometry/Arc2D.h"
#include "Parameter/ParamBands.h"
#include "Parameter/ParamColors.h"
#include "Scale/ScaleMarkings.h"
#include "Scale/ScaleStyle.h"

#include <utility>
#include <vector>

namespace scale {

using Vec2D  = ::geometry::fVector2D;
using Arc2D  = ::geometry::Arc2D<float>;
using RangeF = common::Range<float>;

class Scale
{
public:
    /**
     * Draw one scale arc: minor dashes, coloured bands, major dashes, labels.
     *
     * @param bands  must already be in user units.
     * @return false if the band range is empty, in which case nothing is drawn.
     */
    static bool DrawArc(PainterLike auto &P, const Arc2D &arc, const Markings &markings,
        const parameter::Bands &bands,
        const parameter::gui::Colors &colors = parameter::gui::Colors(),
        const style::Style &style = style::Style());

    /**
     * The airspeed variant: two scales, the coloured arcs, the white flap band
     * and the V-speed markings.
     *
     * An IAS scale usually starts well above zero (60 km/h and up), so the
     * first 20 degrees carry a short pre-scale from zero up to that start. The
     * returned pair is {pre-scale arc, main arc} -- callers need the second one
     * to place the needle.
     */
    static std::pair<Arc2D, Arc2D> DrawArcIAS(PainterLike auto &P, const Arc2D &arc,
        const Markings &markings, const MarkingsIAS &markingsIAS,
        const parameter::Bands &bands,
        const parameter::gui::Colors &colors = parameter::gui::Colors(),
        const style::Style &style = style::Style(),
        const style::StyleIAS &styleIAS = style::StyleIAS());

protected:
    static void DrawBands(PainterLike auto &P, const Arc2D &arc, const style::Style &style,
        const parameter::Bands &bands, const parameter::gui::Colors &colors);

    /* As DrawBands(), but leaves the red band to DrawRedDashesIAS(). */
    static void DrawBandsIAS(PainterLike auto &P, const Arc2D &arc, const style::Style &style,
        const parameter::Bands &bands, const parameter::gui::Colors &colors);

    static void DrawWhiteBand(PainterLike auto &P, const Arc2D &arc, const RangeF &whiteRange,
        const RangeF &range, const style::Band &bandWhite, float fOffset);

    static void DrawRedDashesIAS(PainterLike auto &P, const Arc2D &arc,
        const parameter::Bands &bands, const parameter::gui::Colors &colors,
        const style::Dash &redDash, float fOffset);

    static void DrawVMarkings(PainterLike auto &P, const Arc2D &arc, const style::Style &style,
        const style::StyleIAS &styleIAS, const RangeF &range, const std::vector<VMark> &vMarks);

    static void DrawVMark(PainterLike auto &P, const Arc2D &arc, const style::Style &style,
        const style::StyleIAS &styleIAS, const RangeF &range, const VMark &mark);

    /* Prepares values for drawing a V-speed triangle. */
    static void DrawTriangle(PainterLike auto &P, float fX, const Arc2D &arc, const RangeF &range,
        float fSideLength, float fOffset, bool bDot);

    static void DrawTriangle(PainterLike auto &P, const Vec2D &ptC, float fAngleRad,
        float fRadius, float fLength, bool bDot);

    static void DrawMinorDashes(PainterLike auto &P, const Arc2D &arc, const RangeF &range,
        ::gui::ARGB col, const style::Dash &dashMinor, float fOffset, float fMinorStep);

    static void DrawMajorDashes(PainterLike auto &P, const Arc2D &arc, const RangeF &range,
        ::gui::ARGB col, const style::Dash &dashMajor, float fOffset, float fMajorStep);

    static void DrawLabels(PainterLike auto &P, const Arc2D &arc, const RangeF &range,
        const Markings &markings, float fOffset);

    /* Prepares values for drawing a dash. */
    static void DrawDash(PainterLike auto &P, float fX, const Arc2D &arc,
        const RangeF &range, float fDashLength, float fOffset);

    /* Adds a radial dash for given angle, centre and radii to the current path. */
    static void DrawDash(PainterLike auto &P, const Vec2D &ptC, float fAngleRad, float fR1, float fR2);

    /* Prepares values for drawing a label. */
    static void DrawLabel(PainterLike auto &P, float fX, const Arc2D &arc,
        const RangeF &range, const char *pszText, float fOffset);

    static void DrawLabelAt(PainterLike auto &P, const Vec2D &ptC, float fAngleRad, float fR,
        const char *pszText, bool bInside);

    /* Prepares values for drawing a band. */
    static void DrawBand(PainterLike auto &P, const Arc2D &arc,
        const RangeF &rangeBand, const RangeF &rangeScale, float fR);

    /* Adds a band arc to the current path. Angles are in degrees, Arc2D sense. */
    static void DrawBand(PainterLike auto &P, const Vec2D &ptC, float fR, float fStartDeg, float fSpanDeg);

    /* Angle in degrees, Arc2D sense, for the value x on the scale. */
    static float GetAngleDeg(float fX, float fStartDeg, float fSpanDeg, const RangeF &range)
    { return fStartDeg + range.GetRelative(fX)*fSpanDeg; }

    // CONVERT
    static inline RangeF ConvertToScale(const RangeF &range, float fMultiples)
    {
        return RangeF(
            range.GetLow() / fMultiples,
            range.GetHigh() / fMultiples
        );
    }
};

} // namespace scale
