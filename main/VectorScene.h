#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @file VectorScene.h
 * @brief LVGL UI for the ThorVG vector-graphics demo.
 */

namespace demo {

/**
 * Build the demo screen: a ThorVG-rendered dial on an ARGB8888 canvas,
 * surrounded by ordinary LVGL widgets. Tapping the screen switches scene.
 *
 * Must be called with the LVGL lock held (bsp_display_lock()).
 *
 * @return true when the scene was created, false if the canvas buffer
 *         could not be allocated.
 */
bool CreateScene();

/** Advance to the next scene, exactly as a screen tap would. Takes the LVGL lock itself. */
void ToggleScene();

/** Current scene name: "gauge", "scale", "ias" or "altimeter". */
const char *SceneName();

/** Smoothed ThorVG cost of one frame, in tenths of a millisecond. */
int FrameTimeTenths();

/** Receives raw pixel rows from CaptureScreenshot(). */
using PixelSink = void (*)(const uint8_t *data, size_t len, void *ctx);

/**
 * Render the whole active screen and stream it out as RGB888 rows.
 *
 * The snapshot is taken under the LVGL lock and then released, so streaming
 * (which can take seconds over USB) does not stall the animation.
 *
 * @param step   1 = full resolution, 2 = every other pixel and row, etc.
 * @param sink   called once per output row.
 * @param ctx    passed through to @p sink.
 * @param out_w  receives the emitted width in pixels.
 * @param out_h  receives the emitted height in pixels.
 * @return true on success, false if the snapshot could not be allocated.
 */
bool CaptureScreenshot(int step, PixelSink sink, void *ctx, int32_t *out_w, int32_t *out_h);

} // namespace demo
