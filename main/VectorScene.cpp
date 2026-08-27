/**
 * @file VectorScene.cpp
 * @brief ThorVG vector graphics driven through the lvgl_cpp C++ binding.
 *
 * LVGL 9 ships ThorVG in-tree (src/libs/thorvg) and exposes it as the
 * lv_draw_vector_* C API; pedapudi/lvgl_cpp wraps that as lvgl::VectorDraw and
 * lvgl::VectorPath. Everything here goes through the C++ binding, except two
 * spots where the binding has no equivalent and we reach for dsc.raw().
 */

#include "VectorScene.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "bsp/esp-bsp.h"
#include "lvgl_cpp.h"

#include "AppModel.h"
#include "ScaleDrawTvg.h"
#include "Parameter/ParamBands.h"

#include <cmath>
#include <optional>
#include <vector>

#if !LV_USE_VECTOR_GRAPHIC
#error "Enable CONFIG_LV_USE_VECTOR_GRAPHIC (LVGL -> Others -> Enable Vector Graphic APIs)"
#endif
#if !LV_USE_THORVG_INTERNAL
#error "Enable CONFIG_LV_USE_THORVG and CONFIG_LV_USE_THORVG_INTERNAL"
#endif

namespace {

using lvgl::Align;
using lvgl::Canvas;
using lvgl::Color;
using lvgl::DrawBuf;
using lvgl::Label;
using lvgl::ObjFlag;
using lvgl::Opacity;
using lvgl::Screen;
using lvgl::Timer;
using lvgl::VectorDraw;
using lvgl::VectorPath;

constexpr const char *TAG = "scene";

/* The canvas is ARGB8888 on purpose: lv_draw_sw_vector renders straight into a
 * buffer of that format. Any other format costs a temporary full-area ARGB8888
 * allocation plus a blend-back on every single frame. 400*400*4 = 640 kB, which
 * LVGL's allocator places in PSRAM. */
constexpr int32_t  CANVAS_SIZE = 400;
constexpr float    CTR         = CANVAS_SIZE / 2.0f;
constexpr uint32_t FRAME_MS    = 33;   /* ~30 vector frames/s */

constexpr float DEG2RAD = 3.14159265358979f / 180.0f;

/* The gauge sweeps clockwise from lower-left to lower-right. */
constexpr float ARC_START = 135.0f;
constexpr float ARC_SWEEP = 270.0f;

constexpr uint32_t BG_COLOR = 0x080B14;

/** lv_color32_t is laid out blue, green, red, alpha. */
constexpr lv_color32_t Rgba(uint32_t rgb, uint8_t a = 0xFF)
{
    return lv_color32_t{
        static_cast<uint8_t>(rgb & 0xFFu),
        static_cast<uint8_t>((rgb >> 8) & 0xFFu),
        static_cast<uint8_t>((rgb >> 16) & 0xFFu),
        a,
    };
}

lv_grad_stop_t GradStop(uint32_t rgb, uint8_t frac, lv_opa_t opa = LV_OPA_COVER)
{
    lv_grad_stop_t s{};
    s.color = lv_color_hex(rgb);
    s.opa   = opa;
    s.frac  = frac;
    return s;
}

struct Pt {
    float x;
    float y;
};

/** Point on a circle around the canvas centre. 0 deg = 3 o'clock, growing clockwise. */
Pt Polar(float deg, float radius)
{
    const float a = deg * DEG2RAD;
    return Pt{CTR + radius * std::cos(a), CTR + radius * std::sin(a)};
}

class Scene {
public:
    enum class Mode { Gauge, Rosette, Scale };

    bool Build();

    void NextMode();
    const char *ModeName() const;
    int RenderMsTenths() const { return static_cast<int>(m_fRenderMs * 10.0f + 0.5f); }

private:
    void Tick();
    void DrawGauge(VectorDraw &dsc, VectorPath &path);
    void DrawRosette(VectorDraw &dsc, VectorPath &path);
    void DrawScale(scale::tvg::Painter &P);
    void BuildScale();

    std::optional<Screen>  m_screen;
    std::optional<DrawBuf> m_buf;
    std::optional<Canvas>  m_canvas;
    std::optional<Label>   m_title;
    std::optional<Label>   m_valueLabel;
    std::optional<Label>   m_stats;
    std::optional<Label>   m_hint;
    std::optional<Timer>   m_timer;

    /* Scene 3 is the Kanardia scale, assembled from the shared Common code. */
    parameter::Bands       m_bands;
    parameter::gui::Colors m_colors;
    scale::Markings        m_markings;
    scale::style::Style    m_scaleStyle;
    scale::Arc2D           m_arc;

    Mode  m_eMode      = Mode::Gauge;
    float m_fPhase     = 0.0f;   /* degrees, wraps at 360 */
    float m_fValue     = 0.0f;   /* 0..100, what the gauge shows */
    float m_fRpm       = 0.0f;   /* engine rpm the scale needle points at */
    float m_fRenderMs = 0.0f;   /* smoothed cost of one ThorVG frame */
};

// -----------------------------------------------------------------------------
//  Scene 1: a gauge
// -----------------------------------------------------------------------------

void Scene::DrawGauge(VectorDraw &dsc, VectorPath &path)
{
    /* --- Backing disc, radial gradient ------------------------------------ */
    const std::vector<lv_grad_stop_t> disc_stops = {
        GradStop(0x1E3159, 0),
        GradStop(0x111A33, 150),
        GradStop(BG_COLOR, 255),
    };
    // path.append_circle(CTR, CTR, CTR - 1.0f, CTR - 1.0f);
    // dsc.set_fill_radial_gradient(CTR, CTR, CTR);
    // dsc.set_fill_gradient_stops(disc_stops);
    // dsc.set_fill_gradient_spread(LV_VECTOR_GRADIENT_SPREAD_PAD);
    dsc.set_fill_opa(LV_OPA_COVER);
    // dsc.set_stroke_opa(LV_OPA_TRANSP);
    // dsc.add_path(path);

    /* --- Slowly rotating dashed rim --------------------------------------- */
    // path.clear();
    // path.append_circle(CTR, CTR, CTR - 8.0f, CTR - 8.0f);
    // dsc.set_fill_opa(LV_OPA_TRANSP);
    // dsc.set_stroke_color(Rgba(0x3D5A9E, 0xC0));
    // dsc.set_stroke_opa(LV_OPA_COVER);
    // dsc.set_stroke_width(2.0f);
    // dsc.set_stroke_dash({14.0f, 10.0f});
    // dsc.identity();
    // dsc.translate(CTR, CTR);
    // dsc.rotate(m_fPhase);
    // dsc.translate(-CTR, -CTR);
    // dsc.add_path(path);
    /* The binding's set_stroke_dash() ignores an empty vector, so clear the
     * pattern through the C API or every later stroke stays dashed. */
    // lv_draw_vector_dsc_set_stroke_dash(dsc.raw(), nullptr, 0);
    // dsc.identity();

    /* --- Track arc + value arc -------------------------------------------- */
    const float arc_r = CTR - 34.0f;

    path.clear();
    path.append_arc(CTR, CTR, arc_r, ARC_START, ARC_SWEEP, false);
    dsc.set_stroke_opa(LV_OPA_COVER);
    dsc.set_stroke_color(Rgba(0x1C2540));
    dsc.set_stroke_width(16.0f);
    dsc.set_stroke_cap(LV_VECTOR_STROKE_CAP_ROUND);
    dsc.add_path(path);

    const float sweep = ARC_SWEEP * m_fValue / 100.0f;
    if (sweep > 0.5f) {
        const std::vector<lv_grad_stop_t> arc_stops = {
            GradStop(0x00E5FF, 0),
            GradStop(0x6C5CE7, 140),
            GradStop(0xFF3D9A, 255),
        };
        path.clear();
        path.append_arc(CTR, CTR, arc_r, ARC_START, sweep, false);
        dsc.set_stroke_linear_gradient(0, CANVAS_SIZE, CANVAS_SIZE, 0);
        dsc.set_stroke_gradient_stops(arc_stops);
        dsc.set_stroke_gradient_spread(LV_VECTOR_GRADIENT_SPREAD_PAD);
        dsc.add_path(path);
    }

    /* --- Tick marks: two passes, lit and unlit ---------------------------- */
    constexpr int TICKS = 61;
    const int lit = static_cast<int>(m_fValue * (TICKS - 1) / 100.0f + 0.5f);

    for (int pass = 0; pass < 2; pass++) {
        path.clear();
        bool any = false;
        for (int i = 0; i < TICKS; i++) {
            const bool on = (i <= lit);
            if (on != (pass == 1)) continue;
            const bool major = (i % 5) == 0;
            const float a  = ARC_START + ARC_SWEEP * i / (TICKS - 1);
            const Pt p0 = Polar(a, arc_r - 16.0f);
            const Pt p1 = Polar(a, arc_r - (major ? 34.0f : 26.0f));
            path.move_to(p0.x, p0.y);
            path.line_to(p1.x, p1.y);
            any = true;
        }
        if (!any) continue;
        dsc.set_stroke_color(pass == 1 ? Rgba(0x9FE8FF) : Rgba(0x2E3A5C));
        dsc.set_stroke_width(3.0f);
        dsc.add_path(path);
    }

    /* --- Needle: built pointing up in local space, then rotated ----------- */
    const float r = arc_r - 44.0f;
    const float w = 9.0f;

    path.clear();
    path.move_to(-w, 0.0f);
    path.cubic_to(-w * 0.8f, -r * 0.55f, -2.0f, -r * 0.92f, 0.0f, -r);
    path.cubic_to(2.0f, -r * 0.92f, w * 0.8f, -r * 0.55f, w, 0.0f);
    path.close();

    dsc.set_stroke_opa(LV_OPA_TRANSP);
    dsc.set_fill_color(Rgba(0xFF5C8A));
    dsc.set_fill_opa(LV_OPA_COVER);
    dsc.identity();
    dsc.translate(CTR, CTR);
    dsc.rotate(ARC_START + sweep + 90.0f);
    dsc.add_path(path);
    dsc.identity();

    /* --- Hub ------------------------------------------------------------- */
    path.clear();
    path.append_circle(CTR, CTR, 18.0f, 18.0f);
    dsc.set_fill_color(Rgba(0x0E1424));
    dsc.set_stroke_color(Rgba(0xFF5C8A));
    dsc.set_stroke_opa(LV_OPA_COVER);
    dsc.set_stroke_width(3.0f);
    dsc.add_path(path);
}

// -----------------------------------------------------------------------------
//  Scene 2: a rosette of bezier petals
// -----------------------------------------------------------------------------

void Scene::DrawRosette(VectorDraw &dsc, VectorPath &path)
{
    const std::vector<lv_grad_stop_t> disc_stops = {
        GradStop(0x141E38, 0),
        GradStop(BG_COLOR, 255),
    };
    path.append_circle(CTR, CTR, CTR - 1.0f, CTR - 1.0f);
    dsc.set_fill_radial_gradient(CTR, CTR, CTR);
    dsc.set_fill_gradient_stops(disc_stops);
    dsc.set_fill_opa(LV_OPA_COVER);
    dsc.set_stroke_opa(LV_OPA_TRANSP);
    dsc.add_path(path);

    /* One petal, drawn in local space with the stem at the origin. */
    auto Petal = [&](float len, float width) {
        path.clear();
        path.move_to(0.0f, 0.0f);
        path.cubic_to(width, -len * 0.35f, width * 0.55f, -len * 0.85f, 0.0f, -len);
        path.cubic_to(-width * 0.55f, -len * 0.85f, -width, -len * 0.35f, 0.0f, 0.0f);
        path.close();
    };

    /* Outer ring: 8 petals, additive blending so the overlaps glow. */
    dsc.set_blend_mode(LV_VECTOR_BLEND_ADDITIVE);
    dsc.set_stroke_opa(LV_OPA_TRANSP);
    Petal(CTR - 24.0f, 78.0f);
    for (int i = 0; i < 8; i++) {
        const uint16_t hue = static_cast<uint16_t>(std::fmod(m_fPhase * 1.5f + i * 45.0f, 360.0f));
        dsc.set_fill_color(lv_color_to_32(Color::from_hsv(hue, 85, 50), 0x8C));
        dsc.identity();
        dsc.translate(CTR, CTR);
        dsc.rotate(m_fPhase + i * 45.0f);
        dsc.add_path(path);
    }

    /* Inner ring: 6 petals turning the other way. */
    Petal(CTR * 0.52f, 44.0f);
    for (int i = 0; i < 6; i++) {
        const uint16_t hue = static_cast<uint16_t>(std::fmod(300.0f - m_fPhase * 2.0f + i * 60.0f, 360.0f));
        dsc.set_fill_color(lv_color_to_32(Color::from_hsv(hue, 70, 100), 0xA0));
        dsc.identity();
        dsc.translate(CTR, CTR);
        dsc.rotate(-m_fPhase * 1.7f + i * 60.0f);
        dsc.add_path(path);
    }

    dsc.set_blend_mode(LV_VECTOR_BLEND_SRC_OVER);
    dsc.identity();

    /* Core. */
    path.clear();
    path.append_circle(CTR, CTR, 26.0f, 26.0f);
    dsc.set_fill_color(Rgba(0x0B1020, 0xE0));
    dsc.set_stroke_color(Rgba(0x9FE8FF, 0xB0));
    dsc.set_stroke_opa(LV_OPA_COVER);
    dsc.set_stroke_width(2.0f);
    dsc.add_path(path);
}

// -----------------------------------------------------------------------------
//  Scene 3: a Kanardia scale, drawn by scale::tvg::Scale::DrawArc()
// -----------------------------------------------------------------------------

/**
 * An engine-rpm dial, described the way the rest of the Kanardia code describes
 * one: bands in user units, markings in scale units, and a style holding the
 * dash/band/label geometry. Nothing here is ESP- or LVGL-specific.
 */
void Scene::BuildScale()
{
    /* Bands, in user units (rpm). The first band carries no colour, so no arc
     * is drawn below idle -- only dashes and labels. */
    m_bands.SetLow(0.0f);
    m_bands.Append(1400.0f, parameter::Color::NoColor, 0.0f);
    m_bands.Append(2500.0f, parameter::Color::Green,   0.0f);
    m_bands.Append(2800.0f, parameter::Color::Yellow,  0.0f);
    m_bands.Append(3000.0f, parameter::Color::Red,     0.0f);

    /* Markings are in scale units: rpm/100, so the scale reads 0 .. 30.
     * major 5, five minors per major, a label on every major, no decimals. */
    m_markings = scale::Markings(5.0f, 5, 0.0f, 5.0f, 100.0f, 0);

    /* Everything below the arc radius: bands sit on it, dashes hang inside it,
     * labels sit further in. Offsets are radial and signed. */
    const scale::style::Dash   major {22.0f, 4.0f};
    const scale::style::Dash   minor {12.0f, 2.0f};
    const scale::style::Band   band  {10.0f};
    const scale::style::Font   font  {"Certa Sans", 20, false};
    const scale::style::Offset offset{
        -34.0f,   /* major dash: from R-34 out to R-12 */
        -24.0f,   /* minor dash: from R-24 out to R-12 */
        -58.0f,   /* label centre */
         0.0f,    /* band centred on R */
    };
    m_scaleStyle = scale::style::Style(major, minor, band, font, offset);

    /* Lower-left to lower-right, 270 deg clockwise on screen. Arc2D counts
     * counter-clockwise, hence the negative span. */
    m_arc = scale::Arc2D(scale::Vec2D(CTR, CTR), CTR - 26.0f,
                         common::Rad(225.0f), common::Rad(-270.0f));
}

void Scene::DrawScale(scale::tvg::Painter &P)
{
    VectorDraw &dsc  = P.GetDraw();
    VectorPath &path = P.GetPath();

    /* Dial face. */
    path.append_circle(CTR, CTR, CTR - 1.0f, CTR - 1.0f);
    dsc.set_stroke_opa(LV_OPA_TRANSP);
    dsc.set_fill_color(Rgba(0x0E1424));
    dsc.set_fill_opa(LV_OPA_COVER);
    dsc.add_path(path);
    path.clear();

    /* The scale itself. DrawArc() flushes the vector work before it queues the
     * labels, so anything added after this call lands on top of the text. */
    scale::tvg::Scale::DrawArc(P, m_arc, m_markings, m_bands, m_colors, m_scaleStyle);

    /* Needle, built pointing up in local space. LVGL rotates clockwise while
     * Arc2D angles run counter-clockwise, so the needle angle is 90 - a. */
    const float rel  = m_bands.GetRange().GetRelativeBounded(m_fRpm);
    const float aDeg = common::Deg(m_arc.GetAngle(rel));
    const float r    = m_arc.GetRadius() - 62.0f;
    const float w    = 8.0f;

    path.move_to(-w, 0.0f);
    path.cubic_to(-w * 0.8f, -r * 0.55f, -2.0f, -r * 0.92f, 0.0f, -r);
    path.cubic_to(2.0f, -r * 0.92f, w * 0.8f, -r * 0.55f, w, 0.0f);
    path.close();

    dsc.set_stroke_opa(LV_OPA_TRANSP);
    dsc.set_fill_color(Rgba(0xFF5C8A));
    dsc.set_fill_opa(LV_OPA_COVER);
    dsc.identity();
    dsc.translate(CTR, CTR);
    dsc.rotate(90.0f - aDeg);
    dsc.add_path(path);
    dsc.identity();
    path.clear();

    /* Hub. */
    path.append_circle(CTR, CTR, 16.0f, 16.0f);
    dsc.set_fill_color(Rgba(0x0E1424));
    dsc.set_stroke_color(Rgba(0xFF5C8A));
    dsc.set_stroke_opa(LV_OPA_COVER);
    dsc.set_stroke_width(3.0f);
    dsc.add_path(path);
    path.clear();

    P.Flush();
}

// -----------------------------------------------------------------------------
//  Frame loop
// -----------------------------------------------------------------------------

void Scene::Tick()
{
    m_fPhase += 1.1f;
    if (m_fPhase >= 360.0f) m_fPhase -= 360.0f;

    /* A slow non-linear sweep, just so the gauge has something to show. */
    m_fValue = 50.0f + 48.0f * std::sin(m_fPhase * 2.0f * DEG2RAD);
    /* Engine rpm comes from the flight model, not from here: app::Model feeds
     * its DirectNOD on the 50 ms tick and ModelBase folds it in. Fall back to
     * the sine sweep only if the model loop never started. */
    if (const app::Model *pModel = app::GetModel()) {
        m_fRpm = pModel->GetEngineRPM();
    }
    else {
        m_fRpm = m_bands.GetRange().GetLow()
               + m_bands.GetRange().GetSpan() * m_fValue / 100.0f;
    }

    const int64_t t0 = esp_timer_get_time();

    /* Opaque background: the display blits the canvas without alpha blending. */
    m_canvas->fill_bg(Color(BG_COLOR), LV_OPA_COVER);

    lv_layer_t layer;
    m_canvas->init_layer(&layer);
    if (m_eMode == Mode::Scale) {
        /* The scale wants a Painter -- the same descriptor and path, plus the
         * layer, because its labels go through LVGL's text renderer. */
        scale::tvg::Painter P(&layer);
        DrawScale(P);
    }
    else {
        VectorDraw dsc(&layer);
        VectorPath path(LV_VECTOR_PATH_QUALITY_HIGH);
        if (m_eMode == Mode::Gauge) DrawGauge(dsc, path);
        else                      DrawRosette(dsc, path);
        dsc.draw();
    }
    m_canvas->finish_layer(&layer);  /* waits for the draw units */

    const float ms = static_cast<float>(esp_timer_get_time() - t0) / 1000.0f;
    m_fRenderMs = m_fRenderMs == 0.0f ? ms : m_fRenderMs * 0.9f + ms * 0.1f;

    const int ms_x10 = static_cast<int>(m_fRenderMs * 10.0f + 0.5f);
    m_stats->set_text_fmt("ThorVG  %dx%d  -  %d.%d ms/frame",
                         static_cast<int>(CANVAS_SIZE), static_cast<int>(CANVAS_SIZE),
                         ms_x10 / 10, ms_x10 % 10);

    if (m_eMode == Mode::Gauge) {
        m_valueLabel->set_text_fmt("%d", static_cast<int>(m_fValue + 0.5f));
    }
    else if (m_eMode == Mode::Scale) {
        m_valueLabel->set_text_fmt("%d rpm", static_cast<int>(m_fRpm + 0.5f));
    }

    m_canvas->invalidate();
}

const char *Scene::ModeName() const
{
    switch (m_eMode) {
        case Mode::Gauge:   return "gauge";
        case Mode::Rosette: return "rosette";
        case Mode::Scale:   return "scale";
    }
    return "?";
}

void Scene::NextMode()
{
    switch (m_eMode) {
        case Mode::Gauge:   m_eMode = Mode::Rosette; break;
        case Mode::Rosette: m_eMode = Mode::Scale;   break;
        case Mode::Scale:   m_eMode = Mode::Gauge;   break;
    }

    const bool bValue = (m_eMode != Mode::Rosette);
    m_valueLabel->style().opa(bValue ? Opacity::Cover : Opacity::Transparent);
    m_valueLabel->align(Align::Center, 0, m_eMode == Mode::Scale ? 142 : 78);
    m_hint->set_text_fmt("tap  -  %s", ModeName());
    ESP_LOGI(TAG, "mode -> %s", ModeName());
}

// -----------------------------------------------------------------------------
//  UI construction
// -----------------------------------------------------------------------------

bool Scene::Build()
{
    BuildScale();

    m_screen.emplace(Screen::active());
    m_screen->style().bg_color(Color(BG_COLOR)).bg_opa(Opacity::Cover);
    m_screen->remove_flag(ObjFlag::Scrollable);

    m_buf.emplace(static_cast<uint32_t>(CANVAS_SIZE), static_cast<uint32_t>(CANVAS_SIZE),
                 lvgl::ColorFormat::ARGB8888);
    if (m_buf->raw() == nullptr) {
        ESP_LOGE(TAG, "no room for a %dx%d ARGB8888 canvas (%d kB)",
                 static_cast<int>(CANVAS_SIZE), static_cast<int>(CANVAS_SIZE),
                 static_cast<int>(CANVAS_SIZE * CANVAS_SIZE * 4 / 1024));
        return false;
    }

    m_canvas.emplace(*m_screen);
    m_canvas->set_draw_buf(m_buf->raw());
    m_canvas->fill_bg(Color(BG_COLOR), LV_OPA_COVER);
    m_canvas->center();
    m_canvas->remove_flag(ObjFlag::Clickable);

    m_title.emplace(*m_screen, "ESP32-P4  -  LVGL 9  -  ThorVG");
    m_title->style().text_font(&lv_font_montserrat_20).text_color(Color(0x9FB6E0));
    m_title->align(Align::TopMid, 0, 96);

    m_valueLabel.emplace(*m_screen, "0");
    m_valueLabel->style().text_font(&lv_font_montserrat_28).text_color(Color(0xE8F4FF));
    m_valueLabel->align(Align::Center, 0, 78);

    m_stats.emplace(*m_screen, "");
    m_stats->style().text_font(&lv_font_montserrat_16).text_color(Color(0x6F86B5));
    m_stats->align(Align::BottomMid, 0, -108);

    m_hint.emplace(*m_screen, "tap  -  gauge");
    m_hint->style().text_font(&lv_font_montserrat_16).text_color(Color(0x44557E));
    m_hint->align(Align::BottomMid, 0, -80);

    m_screen->on_click([this](lvgl::Event &) { NextMode(); });
    m_timer.emplace(FRAME_MS, [this](Timer *) { Tick(); });

    ESP_LOGI(TAG, "canvas %dx%d ARGB8888 ready", static_cast<int>(CANVAS_SIZE),
             static_cast<int>(CANVAS_SIZE));
    return true;
}

Scene g_scene;

} // namespace

namespace demo {

bool CreateScene()
{
    return g_scene.Build();
}

void ToggleScene()
{
    bsp_display_lock(UINT32_MAX);
    g_scene.NextMode();
    bsp_display_unlock();
}

const char *SceneName()
{
    return g_scene.ModeName();
}

int FrameTimeTenths()
{
    return g_scene.RenderMsTenths();
}

bool CaptureScreenshot(int step, PixelSink sink, void *ctx, int32_t *out_w, int32_t *out_h)
{
    if (step < 1) step = 1;

    /* Snapshot under the lock, then let the UI carry on while we stream it. */
    bsp_display_lock(UINT32_MAX);
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    bsp_display_unlock();

    if (snap == nullptr) {
        ESP_LOGE(TAG, "snapshot allocation failed");
        return false;
    }

    const int32_t  src_w  = snap->header.w;
    const int32_t  src_h  = snap->header.h;
    const uint32_t stride = snap->header.stride;
    const int32_t  dst_w  = (src_w + step - 1) / step;
    const int32_t  dst_h  = (src_h + step - 1) / step;

    bool ok = true;
    if (step == 1) {
        for (int32_t y = 0; y < src_h && ok; y++) {
            sink(snap->data + static_cast<size_t>(y) * stride,
                 static_cast<size_t>(src_w) * 3, ctx);
        }
    }
    else {
        uint8_t *row = static_cast<uint8_t *>(lv_malloc(static_cast<size_t>(dst_w) * 3));
        if (row == nullptr) {
            ok = false;
        }
        else {
            for (int32_t y = 0; y < src_h; y += step) {
                const uint8_t *src = snap->data + static_cast<size_t>(y) * stride;
                uint8_t *dst = row;
                for (int32_t x = 0; x < src_w; x += step) {
                    const uint8_t *p = src + static_cast<size_t>(x) * 3;
                    *dst++ = p[0];
                    *dst++ = p[1];
                    *dst++ = p[2];
                }
                sink(row, static_cast<size_t>(dst_w) * 3, ctx);
            }
            lv_free(row);
        }
    }

    bsp_display_lock(UINT32_MAX);
    lv_draw_buf_destroy(snap);
    bsp_display_unlock();

    if (out_w) *out_w = dst_w;
    if (out_h) *out_h = dst_h;
    return ok;
}

} // namespace demo
