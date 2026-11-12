#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize touch overlay state
void touch_overlay_init(void);

// Returns true if event was consumed by the touch overlay
bool touch_overlay_handle_event(const SDL_Event *e);

// Render touch overlay on screen using ImGui
void touch_overlay_render(float display_w, float display_h, float scale);

// Toggle or query touch overlay visibility
bool touch_overlay_is_enabled(void);
void touch_overlay_set_enabled(bool enabled);
bool touch_overlay_get_auto_hide(void);
void touch_overlay_set_auto_hide(bool auto_hide);

// User customization: opacity (0.1 - 1.0) and scale (0.5 - 2.0)
float touch_overlay_get_opacity(void);
void touch_overlay_set_opacity(float opacity);
float touch_overlay_get_scale(void);
void touch_overlay_set_scale(float scale);

// Haptic feedback (tactile vibration)
bool touch_overlay_get_haptics(void);
void touch_overlay_set_haptics(bool enabled);

// Layout positioning offsets (relative to default layout)
void touch_overlay_get_offsets(float *lx, float *ly, float *rx, float *ry, float *fx, float *fy, float *dx, float *dy);
void touch_overlay_set_offsets(float lx, float ly, float rx, float ry, float fx, float fy, float dx, float dy);
void touch_overlay_reset_offsets(void);
float touch_overlay_get_safe_inset(void);
void touch_overlay_set_safe_inset(float inset);

// Feed into runtime/hle/input.c
void touch_set_buttons(uint64_t buttons);
void touch_set_sticks(int32_t lx, int32_t ly, int32_t rx, int32_t ry);

#ifdef __cplusplus
}
#endif
