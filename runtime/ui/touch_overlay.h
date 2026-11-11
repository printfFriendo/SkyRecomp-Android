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

// Feed into runtime/hle/input.c
void touch_set_buttons(uint64_t buttons);
void touch_set_sticks(int32_t lx, int32_t ly, int32_t rx, int32_t ry);

#ifdef __cplusplus
}
#endif
