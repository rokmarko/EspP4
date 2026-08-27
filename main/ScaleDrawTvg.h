#pragma once

/**
 * @file ScaleDrawTvg.h
 * @brief ThorVG counterpart of Common/Scale/ScaleDrawQt.h.
 *
 * Same scale, same style structs, same geometry -- only the back end differs.
 * Where the Qt version paints into a QPainter, this one appends paths to an
 * lv_draw_vector_dsc_t (rasterised by ThorVG inside LVGL) and hands the labels
 * to LVGL's own text renderer, because ThorVG has no text API here.
 *
 * Angles follow Arc2D: radians, zero at 3 o'clock, growing counter-clockwise on
 * screen. LVGL's vector API measures degrees clockwise, so every angle handed
 * to it is negated.
 */

#include "KanardiaCommon.h"

#include "lvgl.h"
#include "lvgl_cpp.h"

#include "Geometry/Arc2D.h"
#include "Parameter/ParamColors.h"
#include "Scale/ScaleMarkings.h"
#include "Scale/ScaleStyle.h"

#include <utility>
#include <vector>

namespace parameter { class Bands; }

namespace scale {

using Vec2D  = ::geometry::fVector2D;
using Arc2D  = ::geometry::Arc2D<float>;
using RangeF = common::Range<float>;

namespace tvg {

/**
 * ARGB as used all over Common -> lv_color32_t, which is laid out b,g,r,a.
 *
 * The alpha byte is dropped on purpose. Common's colour constants carry no
 * alpha (C32_WHITE is plain 0xffffff), and the Qt back end converts them with
 * QColor(QRgb), which is opaque by definition -- so opaque is what a scale
 * colour has always meant. Honouring the byte instead renders every white dash
 * and label at opacity zero.
 */
constexpr lv_color32_t ToColor32(::gui::ARGB c)
{
    return lv_color32_t{
        static_cast<uint8_t>(::gui::GetBlue(c)),
        static_cast<uint8_t>(::gui::GetGreen(c)),
        static_cast<uint8_t>(::gui::GetRed(c)),
        0xFF,
    };
}

/**
 * What QPainter is to ScaleDrawQt: the drawing target plus the current pen.
 *
 * It owns the LVGL layer's vector descriptor and one scratch path that every
 * primitive reuses. Strokes sharing a pen are accumulated into that single path
 * and rasterised in one go -- on this board a ThorVG frame costs 100+ ms, so
 * the number of paths matters more than anything else here.
 */
class Painter {
public:
    explicit Painter(lv_layer_t *pLayer) : m_pLayer(pLayer), m_dsc(pLayer) {}

    lv_layer_t       *GetLayer() { return m_pLayer; }
    lvgl::VectorDraw &GetDraw()  { return m_dsc; }
    lvgl::VectorPath &GetPath()  { return m_path; }

    /** Stroke @p argb at @p fWidth with butt caps and no fill -- Qt's flat pen. */
    void SetPen(::gui::ARGB argb, float fWidth);
    void SetPen(::gui::ARGB argb, const style::Dash &dash) { SetPen(argb, dash.m_fWidth); }
    void SetPen(::gui::ARGB argb, const style::Band &band) { SetPen(argb, band.m_fWidth); }

    /** Fill with @p argb and stroke nothing -- Qt's brush with a null pen. */
    void SetBrush(::gui::ARGB argb);

    /** Colour the labels are drawn in; follows the pen, as it does in Qt. */
    ::gui::ARGB GetPenColor() const { return m_penColor; }

    /** Pick the built-in LVGL font closest to @p font. */
    void SetFont(const style::Font &font) { m_pFont = CreateFont(font); }
    const lv_font_t *GetFont() const { return m_pFont; }

    /**
     * Hand the scratch path to the descriptor with whatever pen or brush is
     * currently set, and clear it. Nothing reaches ThorVG until Flush().
     */
    void Emit();

    /** Queue everything accumulated so far as one vector draw task. */
    void Flush() { m_dsc.draw(); }

    /**
     * Nearest built-in Montserrat to @p font. The family name is ignored:
     * this build has no font engine, only the sizes LVGL was compiled with.
     */
    static const lv_font_t *CreateFont(const style::Font &font);

private:
    lv_layer_t       *m_pLayer;
    lvgl::VectorDraw  m_dsc;
    lvgl::VectorPath  m_path{LV_VECTOR_PATH_QUALITY_HIGH};
    const lv_font_t  *m_pFont    = nullptr;
    ::gui::ARGB       m_penColor = 0xFFFFFFFF;
};

// --------------------------------------------------------------------------

class Scale
{
public:
    /**
     * Draw one scale arc: minor dashes, coloured bands, major dashes, labels.
     *
     * @param bands  must already be in user units, exactly as in the Qt version.
     * @return false if the band range is empty, in which case nothing is drawn.
     */
    static bool DrawArc(Painter &P, const Arc2D &arc, const Markings &markings,
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
    static std::pair<Arc2D, Arc2D> DrawArcIAS(Painter &P, const Arc2D &arc,
        const Markings &markings, const MarkingsIAS &markingsIAS,
        const parameter::Bands &bands,
        const parameter::gui::Colors &colors = parameter::gui::Colors(),
        const style::Style &style = style::Style(),
        const style::StyleIAS &styleIAS = style::StyleIAS());

protected:
    static void DrawBands(Painter &P, const Arc2D &arc, const style::Style &style,
        const parameter::Bands &bands, const parameter::gui::Colors &colors);

    /* As DrawBands(), but leaves the red band to DrawRedDashesIAS(). */
    static void DrawBandsIAS(Painter &P, const Arc2D &arc, const style::Style &style,
        const parameter::Bands &bands, const parameter::gui::Colors &colors);

    static void DrawWhiteBand(Painter &P, const Arc2D &arc, const RangeF &whiteRange,
        const RangeF &range, const style::Band &bandWhite, float fOffset);

    static void DrawRedDashesIAS(Painter &P, const Arc2D &arc,
        const parameter::Bands &bands, const parameter::gui::Colors &colors,
        const style::Dash &redDash, float fOffset);

    static void DrawVMarkings(Painter &P, const Arc2D &arc, const style::Style &style,
        const style::StyleIAS &styleIAS, const RangeF &range, const std::vector<VMark> &vMarks);

    static void DrawVMark(Painter &P, const Arc2D &arc, const style::Style &style,
        const style::StyleIAS &styleIAS, const RangeF &range, const VMark &mark);

    /* Prepares values for drawing a V-speed triangle. */
    static void DrawTriangle(Painter &P, float fX, const Arc2D &arc, const RangeF &range,
        float fSideLength, float fOffset, bool bDot);

    static void DrawTriangle(Painter &P, const Vec2D &ptC, float fAngleRad,
        float fRadius, float fLength, bool bDot);

    static void DrawMinorDashes(Painter &P, const Arc2D &arc, const RangeF &range,
        ::gui::ARGB col, const style::Dash &dashMinor, float fOffset, float fMinorStep);

    static void DrawMajorDashes(Painter &P, const Arc2D &arc, const RangeF &range,
        ::gui::ARGB col, const style::Dash &dashMajor, float fOffset, float fMajorStep);

    static void DrawLabels(Painter &P, const Arc2D &arc, const RangeF &range,
        const Markings &markings, float fOffset);

    /* Prepares values for drawing a dash. */
    static void DrawDash(Painter &P, float fX, const Arc2D &arc,
        const RangeF &range, float fDashLength, float fOffset);

    /* Adds a radial dash for given angle, centre and radii to the scratch path. */
    static void DrawDash(Painter &P, const Vec2D &ptC, float fAngleRad, float fR1, float fR2);

    /* Prepares values for drawing a label. */
    static void DrawLabel(Painter &P, float fX, const Arc2D &arc,
        const RangeF &range, const char *pszText, float fOffset);

    static void DrawLabelAt(Painter &P, const Vec2D &ptC, float fAngleRad, float fR,
        const char *pszText, bool bInside);

    /* Prepares values for drawing a band. */
    static void DrawBand(Painter &P, const Arc2D &arc,
        const RangeF &rangeBand, const RangeF &rangeScale, float fR);

    /* Adds a band arc to the scratch path. Angles are in degrees, Arc2D sense. */
    static void DrawBand(Painter &P, const Vec2D &ptC, float fR, float fStartDeg, float fSpanDeg);

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

}} // namespace scale::tvg
