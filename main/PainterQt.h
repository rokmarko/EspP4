#pragma once

/**
 * @file PainterQt.h
 * @brief QPainter back end for the PainterLike concept.
 *
 * Nothing in this firmware compiles it -- there is no Qt on the board. It
 * exists so the concept has two independent implementations: an interface with
 * one back end drifts into being that back end's shape with extra steps, and
 * the only way to know ScaleDraw.cpp is really portable is to build it against
 * something that is not ThorVG. It is also what would move into
 * Common/Scale/ if the Qt products ever adopt the shared body.
 *
 * Two Qt conventions are worth stating, because they disagree with each other:
 * QPainter::drawArc() and QPainterPath::arcTo() measure counter-clockwise from
 * 3 o'clock -- the same sense as Arc2D, so arcs need no adjustment -- while
 * QPainter::rotate() is positive clockwise on a y-down device, so rotations do.
 */

#if defined(QT_CORE_LIB)

#include "Painter.h"

#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QString>

namespace scale {

class PainterQt {
public:
    explicit PainterQt(QPainter &p) : m_rP(p) {}

    PainterQt(const PainterQt &) = delete;
    PainterQt &operator=(const PainterQt &) = delete;

    // -- PainterLike ------------------------------------------------------

    /**
     * Flat cap is what every Kanardia scale pen has always used; a round cap
     * would make short minor dashes visibly longer than they are.
     *
     * QColor(QRgb) is opaque regardless of the alpha byte, which is what makes
     * Common's C32_* constants -- all of which carry a zero alpha -- render at
     * all. The ThorVG back end forces the same by hand.
     */
    void SetPen(::gui::ARGB argb, float fWidth)
    {
        m_pen  = QPen(QColor(QRgb(argb)), fWidth, Qt::SolidLine, Qt::FlatCap);
        m_eMode = Mode::Stroke;
    }

    void SetBrush(::gui::ARGB argb)
    {
        m_brush = QBrush(QColor(QRgb(argb)));
        m_eMode = Mode::Fill;
    }

    void SetFont(const style::Font &font)
    {
        QFont f(QString::fromStdString(font.m_ssFamily));
        f.setPixelSize(font.m_iSize);
        f.setBold(font.m_bBold);
        m_rP.setFont(f);
    }

    void MoveTo(float fX, float fY) { m_path.moveTo(fX, fY); }
    void LineTo(float fX, float fY) { m_path.lineTo(fX, fY); }
    void ClosePath()                { m_path.closeSubpath(); }

    /** arcMoveTo() first, or arcTo() joins the arc to wherever the path was. */
    void AppendArc(float fCX, float fCY, float fR, float fStartDeg, float fSpanDeg)
    {
        const QRectF rc(fCX - fR, fCY - fR, 2*fR, 2*fR);
        m_path.arcMoveTo(rc, fStartDeg);
        m_path.arcTo(rc, fStartDeg, fSpanDeg);
    }

    void AppendCircle(float fCX, float fCY, float fR)
    {
        m_path.addEllipse(QPointF(fCX, fCY), fR, fR);
    }

    void PushTransform(float fTX, float fTY, float fRotDeg)
    {
        m_rP.save();
        m_rP.translate(fTX, fTY);
        m_rP.rotate(-fRotDeg);
    }

    void PopTransform() { m_rP.restore(); }

    Size2D TextSize(const char *pszText) const
    {
        const QRect rc = m_rP.fontMetrics().tightBoundingRect(QString::fromUtf8(pszText));
        return {static_cast<float>(rc.width()), static_cast<float>(rc.height())};
    }

    /**
     * Place the centre of the text box at (@p fCX, @p fCY), in white.
     *
     * Qt anchors text on the baseline, so the box centre has to be converted:
     * iZ is the (negative) distance from baseline to box centre.
     */
    void DrawTextCentred(float fCX, float fCY, const char *pszText)
    {
        const QString qs = QString::fromUtf8(pszText);
        const QRect   rc = m_rP.fontMetrics().tightBoundingRect(qs);
        const int     iZ = rc.top() + rc.height()/2;

        const QPoint pt(qRound(fCX) - rc.width()/2 - 1, qRound(fCY) - iZ);
        DrawTextAsPath(pt, qs);
    }

    /** Stroke or fill what has been built, per the current pen or brush. */
    void Emit()
    {
        if (m_eMode == Mode::Stroke)
            m_rP.strokePath(m_path, m_pen);
        else
            m_rP.fillPath(m_path, m_brush);

        m_path = QPainterPath();
    }

    /** Qt has one immediate-mode pipeline; there is nothing to sequence. */
    void Flush() {}

private:
    enum class Mode { Stroke, Fill };

    /**
     * Raster devices draw the glyphs; SVG and PDF get them as outlines, so the
     * text stays vector data in an exported file rather than an embedded font.
     */
    void DrawTextAsPath(const QPoint &pt, const QString &qs)
    {
#if defined(EMBEDDED_LINUX)
        m_rP.setPen(Qt::white);
        m_rP.setBrush(Qt::white);
        m_rP.drawText(pt, qs);
#else
        const auto eType = m_rP.device()->paintEngine()->type();
        const bool bVector = eType == QPaintEngine::SVG || eType == QPaintEngine::Pdf;
        if (bVector == false) {
            m_rP.setPen(Qt::white);
            m_rP.setBrush(Qt::white);
            m_rP.drawText(pt, qs);
        }
        else {
            auto font = m_rP.font();
            font.setStyleStrategy(QFont::PreferAntialias);
            m_rP.setPen(Qt::NoPen);
            m_rP.setBrush(Qt::white);
            QPainterPath textPath;
            textPath.addText(pt, font, qs);
            m_rP.drawPath(textPath);
        }
#endif
    }

    QPainter     &m_rP;
    QPainterPath  m_path;
    QPen          m_pen{Qt::white};
    QBrush        m_brush{Qt::white};
    Mode          m_eMode = Mode::Stroke;
};

static_assert(PainterLike<PainterQt>);

} // namespace scale

#endif /* QT_CORE_LIB */
