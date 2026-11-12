#include "touch_overlay.h"
#include <imgui.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
static void trigger_touch_haptic(void) {
    if (touch_overlay_get_haptics()) {
        SDL_SendAndroidMessage(0x8001, 0);
    }
}
#else
static void trigger_touch_haptic(void) {}
#endif

extern "C" void menu_toggle(void);
extern "C" bool input_has_gamepad(void);

enum {
    BTN_A = 0, BTN_B, BTN_X, BTN_Y, BTN_LSTICK, BTN_RSTICK, BTN_L, BTN_R, BTN_ZL, BTN_ZR, BTN_PLUS, BTN_MINUS,
    BTN_LEFT, BTN_UP, BTN_RIGHT, BTN_DOWN,
    CTRL_LSTICK = 16,
    CTRL_RSTICK = 17,
    CTRL_MENU = 18,
    CTRL_COUNT = 19
};

struct TouchFinger {
    SDL_FingerID id;
    float start_x, start_y;
    float current_x, current_y;
    int control_id;
    bool active;
};

#define MAX_FINGERS 10
static TouchFinger g_fingers[MAX_FINGERS];
static bool g_enabled = true;
static bool g_auto_hide = false;
static bool g_user_haptics = true;
static float g_user_opacity = 0.65f;
static float g_user_scale = 1.0f;
static float g_safe_inset = 0.0f;
static float g_lstick_x = 0.0f, g_lstick_y = 0.0f;
static float g_rstick_x = 0.0f, g_rstick_y = 0.0f;
static float g_face_x = 0.0f, g_face_y = 0.0f;
static float g_dpad_x = 0.0f, g_dpad_y = 0.0f;
static uint64_t g_active_buttons = 0;
static int32_t g_stick_lx = 0, g_stick_ly = 0;
static int32_t g_stick_rx = 0, g_stick_ry = 0;
static float g_cur_w = 1920.0f, g_cur_h = 1080.0f;

static uint64_t g_last_lstick_tap = 0;
static uint64_t g_last_rstick_tap = 0;
static uint64_t g_stick_click_until = 0;
static int g_stick_clicked_btn = -1;

float touch_overlay_get_opacity(void) { return g_user_opacity; }
void touch_overlay_set_opacity(float opacity) {
    if (opacity < 0.1f) opacity = 0.1f;
    if (opacity > 1.0f) opacity = 1.0f;
    g_user_opacity = opacity;
}

float touch_overlay_get_scale(void) { return g_user_scale; }
void touch_overlay_set_scale(float scale) {
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 2.0f) scale = 2.0f;
    g_user_scale = scale;
}

bool touch_overlay_get_haptics(void) { return g_user_haptics; }
void touch_overlay_set_haptics(bool enabled) { g_user_haptics = enabled; }

void touch_overlay_get_offsets(float *lx, float *ly, float *rx, float *ry, float *fx, float *fy, float *dx, float *dy) {
    if (lx) *lx = g_lstick_x;
    if (ly) *ly = g_lstick_y;
    if (rx) *rx = g_rstick_x;
    if (ry) *ry = g_rstick_y;
    if (fx) *fx = g_face_x;
    if (fy) *fy = g_face_y;
    if (dx) *dx = g_dpad_x;
    if (dy) *dy = g_dpad_y;
}

void touch_overlay_set_offsets(float lx, float ly, float rx, float ry, float fx, float fy, float dx, float dy) {
    g_lstick_x = lx;
    g_lstick_y = ly;
    g_rstick_x = rx;
    g_rstick_y = ry;
    g_face_x = fx;
    g_face_y = fy;
    g_dpad_x = dx;
    g_dpad_y = dy;
}

float touch_overlay_get_safe_inset(void) { return g_safe_inset; }
void touch_overlay_set_safe_inset(float inset) {
    if (inset < 0.0f) inset = 0.0f;
    if (inset > 120.0f) inset = 120.0f;
    g_safe_inset = inset;
}

void touch_overlay_reset_offsets(void) {
    g_safe_inset = 0.0f;
    g_lstick_x = g_lstick_y = 0.0f;
    g_rstick_x = g_rstick_y = 0.0f;
    g_face_x = g_face_y = 0.0f;
    g_dpad_x = g_dpad_y = 0.0f;
}

static inline ImU32 modulate_alpha(ImU32 col, float factor) {
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    c.w *= factor;
    return ImGui::ColorConvertFloat4ToU32(c);
}

void touch_overlay_init(void) {
    memset(g_fingers, 0, sizeof(g_fingers));
    g_active_buttons = 0;
    g_stick_lx = g_stick_ly = 0;
    g_stick_rx = g_stick_ry = 0;
    touch_set_buttons(0);
    touch_set_sticks(0, 0, 0, 0);
}

bool touch_overlay_is_enabled(void) {
    return g_enabled;
}

void touch_overlay_set_enabled(bool enabled) {
    g_enabled = enabled;
    if (!enabled) {
        touch_overlay_init();
    }
}

bool touch_overlay_get_auto_hide(void) {
    return g_auto_hide;
}

void touch_overlay_set_auto_hide(bool auto_hide) {
    g_auto_hide = auto_hide;
}

static inline float dist_sq(float x1, float y1, float x2, float y2) {
    float dx = x1 - x2;
    float dy = y1 - y2;
    return dx * dx + dy * dy;
}

static int hit_test(float px, float py, float W, float H, float s) {
    s *= g_user_scale;
    // 1. Left Stick (Base center)
    float lx = (140.0f + g_lstick_x + g_safe_inset) * s, ly = (H - 140.0f * s) + g_lstick_y * s, lr = 90.0f * s;
    if (dist_sq(px, py, lx, ly) <= lr * lr) return CTRL_LSTICK;

    // 2. Right Stick (Base center)
    float rx = (W - (280.0f + g_safe_inset) * s) + g_rstick_x * s, ry = (H - 140.0f * s) + g_rstick_y * s, rr = 80.0f * s;
    if (dist_sq(px, py, rx, ry) <= rr * rr) return CTRL_RSTICK;

    // 3. Face buttons (A, B, X, Y)
    float fcx = (W - (110.0f + g_safe_inset) * s) + g_face_x * s, fcy = (H - 140.0f * s) + g_face_y * s;
    float foff = 46.0f * s, fr = 32.0f * s;
    if (dist_sq(px, py, fcx + foff, fcy) <= fr * fr) return BTN_A;
    if (dist_sq(px, py, fcx, fcy + foff) <= fr * fr) return BTN_B;
    if (dist_sq(px, py, fcx, fcy - foff) <= fr * fr) return BTN_X;
    if (dist_sq(px, py, fcx - foff, fcy) <= fr * fr) return BTN_Y;

    // 4. D-Pad (Up, Down, Left, Right)
    float dcx = (140.0f + g_dpad_x + g_safe_inset) * s, dcy = (H - 290.0f * s) + g_dpad_y * s;
    float doff = 42.0f * s, dr = 28.0f * s;
    if (dist_sq(px, py, dcx, dcy - doff) <= dr * dr) return BTN_UP;
    if (dist_sq(px, py, dcx, dcy + doff) <= dr * dr) return BTN_DOWN;
    if (dist_sq(px, py, dcx - doff, dcy) <= dr * dr) return BTN_LEFT;
    if (dist_sq(px, py, dcx + doff, dcy) <= dr * dr) return BTN_RIGHT;

    // 5. Shoulder buttons (L, ZL, R, ZR)
    float sh_w = 90.0f * s, sh_h = 42.0f * s;
    // L
    if (px >= (30.0f + g_safe_inset) * s && px <= (30.0f + g_safe_inset) * s + sh_w && py >= 30.0f * s && py <= 30.0f * s + sh_h) return BTN_L;
    // ZL
    if (px >= (135.0f + g_safe_inset) * s && px <= (135.0f + g_safe_inset) * s + sh_w && py >= 25.0f * s && py <= 25.0f * s + sh_h) return BTN_ZL;
    // R
    if (px >= W - (30.0f + g_safe_inset) * s - sh_w && px <= W - (30.0f + g_safe_inset) * s && py >= 30.0f * s && py <= 30.0f * s + sh_h) return BTN_R;
    // ZR
    if (px >= W - (135.0f + g_safe_inset) * s - sh_w && px <= W - (135.0f + g_safe_inset) * s && py >= 25.0f * s && py <= 25.0f * s + sh_h) return BTN_ZR;

    // 6. Utility (Minus, Menu, Plus)
    float ut_r = 26.0f * s;
    if (dist_sq(px, py, W * 0.40f, 40.0f * s) <= ut_r * ut_r) return BTN_MINUS;
    if (dist_sq(px, py, W * 0.60f, 40.0f * s) <= ut_r * ut_r) return BTN_PLUS;
    if (dist_sq(px, py, W * 0.50f, 40.0f * s) <= ut_r * ut_r) return CTRL_MENU;

    return -1;
}

static void update_state(float W, float H, float s) {
    s *= g_user_scale;
    uint64_t buttons = 0;
    int32_t lx = 0, ly = 0, rx = 0, ry = 0;

    float l_cx = (140.0f + g_lstick_x + g_safe_inset) * s, l_cy = (H - 140.0f * s) + g_lstick_y * s, l_r = 75.0f * s;
    float r_cx = (W - (280.0f + g_safe_inset) * s) + g_rstick_x * s, r_cy = (H - 140.0f * s) + g_rstick_y * s, r_r = 65.0f * s;

    for (int i = 0; i < MAX_FINGERS; i++) {
        if (!g_fingers[i].active) continue;
        int cid = g_fingers[i].control_id;
        if (cid >= 0 && cid < 16) {
            buttons |= (1ull << cid);
        } else if (cid == CTRL_LSTICK) {
            float dx = g_fingers[i].current_x - l_cx;
            float dy = g_fingers[i].current_y - l_cy;
            float len = sqrtf(dx * dx + dy * dy);
            float deadzone = 0.12f * l_r;
            if (len <= deadzone) {
                lx = 0;
                ly = 0;
            } else {
                float norm_len = (len - deadzone) / (l_r - deadzone);
                if (norm_len > 1.0f) norm_len = 1.0f;
                float dir_x = dx / len;
                float dir_y = dy / len;
                lx = (int32_t)(dir_x * norm_len * 32767.0f);
                ly = (int32_t)(-dir_y * norm_len * 32767.0f); // Invert Y for Switch convention (Up is positive)
            }
        } else if (cid == CTRL_RSTICK) {
            float dx = g_fingers[i].current_x - r_cx;
            float dy = g_fingers[i].current_y - r_cy;
            float len = sqrtf(dx * dx + dy * dy);
            float deadzone = 0.12f * r_r;
            if (len <= deadzone) {
                rx = 0;
                ry = 0;
            } else {
                float norm_len = (len - deadzone) / (r_r - deadzone);
                if (norm_len > 1.0f) norm_len = 1.0f;
                float dir_x = dx / len;
                float dir_y = dy / len;
                rx = (int32_t)(dir_x * norm_len * 32767.0f);
                ry = (int32_t)(-dir_y * norm_len * 32767.0f);
            }
        }
    }

    if (SDL_GetTicks() < g_stick_click_until && g_stick_clicked_btn >= 0) {
        buttons |= (1ull << g_stick_clicked_btn);
    }

    g_active_buttons = buttons;
    g_stick_lx = lx;
    g_stick_ly = ly;
    g_stick_rx = rx;
    g_stick_ry = ry;

    touch_set_buttons(buttons);
    touch_set_sticks(lx, ly, rx, ry);
}

bool touch_overlay_handle_event(const SDL_Event *e) {
    if (!g_enabled) return false;
    if (g_auto_hide && input_has_gamepad()) return false;

    float W = g_cur_w, H = g_cur_h;
    float s = H / 1080.0f;
    if (s < 0.6f) s = 0.6f;

    switch (e->type) {
    case SDL_EVENT_FINGER_DOWN: {
        float px = e->tfinger.x * W;
        float py = e->tfinger.y * H;
        int cid = hit_test(px, py, W, H, s);
        if (cid < 0) return false;

        trigger_touch_haptic();

        if (cid == CTRL_MENU) {
            menu_toggle();
            return true;
        }

        if (cid == CTRL_LSTICK) {
            uint64_t now = SDL_GetTicks();
            if (now - g_last_lstick_tap < 350) {
                g_stick_clicked_btn = BTN_LSTICK;
                g_stick_click_until = now + 180;
                g_last_lstick_tap = 0;
            } else {
                g_last_lstick_tap = now;
            }
        } else if (cid == CTRL_RSTICK) {
            uint64_t now = SDL_GetTicks();
            if (now - g_last_rstick_tap < 350) {
                g_stick_clicked_btn = BTN_RSTICK;
                g_stick_click_until = now + 180;
                g_last_rstick_tap = 0;
            } else {
                g_last_rstick_tap = now;
            }
        }

        for (int i = 0; i < MAX_FINGERS; i++) {
            if (!g_fingers[i].active) {
                g_fingers[i].id = e->tfinger.fingerID;
                g_fingers[i].start_x = px;
                g_fingers[i].start_y = py;
                g_fingers[i].current_x = px;
                g_fingers[i].current_y = py;
                g_fingers[i].control_id = cid;
                g_fingers[i].active = true;
                break;
            }
        }
        update_state(W, H, s);
        return true;
    }
    case SDL_EVENT_FINGER_MOTION: {
        float px = e->tfinger.x * W;
        float py = e->tfinger.y * H;
        bool found = false;
        for (int i = 0; i < MAX_FINGERS; i++) {
            if (g_fingers[i].active && g_fingers[i].id == e->tfinger.fingerID) {
                g_fingers[i].current_x = px;
                g_fingers[i].current_y = py;
                found = true;
                break;
            }
        }
        if (found) {
            update_state(W, H, s);
            return true;
        }
        return false;
    }
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        bool found = false;
        for (int i = 0; i < MAX_FINGERS; i++) {
            if (g_fingers[i].active && g_fingers[i].id == e->tfinger.fingerID) {
                g_fingers[i].active = false;
                found = true;
                break;
            }
        }
        if (found) {
            update_state(W, H, s);
            return true;
        }
        return false;
    }
    default:
        break;
    }
    return false;
}

static void draw_btn(ImDrawList *dl, float cx, float cy, float r, const char *label, bool active, ImU32 col_active) {
    ImU32 bg = active ? col_active : IM_COL32(30, 30, 30, 110);
    ImU32 border = active ? IM_COL32(255, 255, 255, 220) : IM_COL32(200, 200, 200, 130);
    bg = modulate_alpha(bg, g_user_opacity);
    border = modulate_alpha(border, g_user_opacity);
    dl->AddCircleFilled(ImVec2(cx, cy), r, bg);
    dl->AddCircle(ImVec2(cx, cy), r, border, 0, 2.0f);

    ImVec2 txt_sz = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(cx - txt_sz.x * 0.5f, cy - txt_sz.y * 0.5f), modulate_alpha(IM_COL32(255, 255, 255, 220), g_user_opacity), label);
}

static void draw_capsule_btn(ImDrawList *dl, float x, float y, float w, float h, const char *label, bool active) {
    ImU32 bg = active ? IM_COL32(236, 194, 84, 180) : IM_COL32(30, 30, 30, 110);
    ImU32 border = active ? IM_COL32(255, 255, 255, 220) : IM_COL32(200, 200, 200, 130);
    bg = modulate_alpha(bg, g_user_opacity);
    border = modulate_alpha(border, g_user_opacity);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), bg, h * 0.5f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), border, h * 0.5f, 0, 2.0f);

    ImVec2 txt_sz = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(x + (w - txt_sz.x) * 0.5f, y + (h - txt_sz.y) * 0.5f), modulate_alpha(IM_COL32(255, 255, 255, 220), g_user_opacity), label);
}

void touch_overlay_render(float display_w, float display_h, float scale) {
    if (!g_enabled) return;
    if (g_auto_hide && input_has_gamepad()) return;

    g_cur_w = display_w;
    g_cur_h = display_h;
    float W = display_w, H = display_h;
    float s = scale * g_user_scale;
    if (s < 0.5f) s = 0.5f;

    ImDrawList *dl = ImGui::GetForegroundDrawList();

    // 1. Left Analog Stick (Movement)
    float lx = (140.0f + g_lstick_x + g_safe_inset) * s, ly = (H - 140.0f * s) + g_lstick_y * s, lr = 75.0f * s;
    dl->AddCircleFilled(ImVec2(lx, ly), lr, modulate_alpha(IM_COL32(25, 25, 25, 80), g_user_opacity));
    dl->AddCircle(ImVec2(lx, ly), lr, modulate_alpha(IM_COL32(180, 180, 180, 100), g_user_opacity), 0, 2.0f);

    float thumb_lx = lx + ((float)g_stick_lx / 32767.0f) * lr;
    float thumb_ly = ly - ((float)g_stick_ly / 32767.0f) * lr;
    dl->AddCircleFilled(ImVec2(thumb_lx, thumb_ly), 32.0f * s, modulate_alpha(IM_COL32(200, 200, 200, 140), g_user_opacity));
    dl->AddCircle(ImVec2(thumb_lx, thumb_ly), 32.0f * s, modulate_alpha(IM_COL32(255, 255, 255, 200), g_user_opacity), 0, 2.0f);

    // 2. Right Analog Stick (Camera / Sword)
    float rx = (W - (280.0f + g_safe_inset) * s) + g_rstick_x * s, ry = (H - 140.0f * s) + g_rstick_y * s, rr = 65.0f * s;
    dl->AddCircleFilled(ImVec2(rx, ry), rr, modulate_alpha(IM_COL32(25, 25, 25, 80), g_user_opacity));
    dl->AddCircle(ImVec2(rx, ry), rr, modulate_alpha(IM_COL32(180, 180, 180, 100), g_user_opacity), 0, 2.0f);

    float thumb_rx = rx + ((float)g_stick_rx / 32767.0f) * rr;
    float thumb_ry = ry - ((float)g_stick_ry / 32767.0f) * rr;
    dl->AddCircleFilled(ImVec2(thumb_rx, thumb_ry), 28.0f * s, modulate_alpha(IM_COL32(200, 200, 200, 140), g_user_opacity));
    dl->AddCircle(ImVec2(thumb_rx, thumb_ry), 28.0f * s, modulate_alpha(IM_COL32(255, 255, 255, 200), g_user_opacity), 0, 2.0f);

    // 3. Face buttons (A, B, X, Y)
    float fcx = (W - (110.0f + g_safe_inset) * s) + g_face_x * s, fcy = (H - 140.0f * s) + g_face_y * s;
    float foff = 46.0f * s, fr = 26.0f * s;
    draw_btn(dl, fcx + foff, fcy, fr, "A", (g_active_buttons & (1ull << BTN_A)) != 0, IM_COL32(230, 50, 50, 190));
    draw_btn(dl, fcx, fcy + foff, fr, "B", (g_active_buttons & (1ull << BTN_B)) != 0, IM_COL32(230, 180, 40, 190));
    draw_btn(dl, fcx, fcy - foff, fr, "X", (g_active_buttons & (1ull << BTN_X)) != 0, IM_COL32(50, 120, 230, 190));
    draw_btn(dl, fcx - foff, fcy, fr, "Y", (g_active_buttons & (1ull << BTN_Y)) != 0, IM_COL32(40, 190, 80, 190));

    // 4. D-Pad (Up, Down, Left, Right)
    float dcx = (140.0f + g_dpad_x + g_safe_inset) * s, dcy = (H - 290.0f * s) + g_dpad_y * s;
    float doff = 42.0f * s, dr = 24.0f * s;
    draw_btn(dl, dcx, dcy - doff, dr, "^", (g_active_buttons & (1ull << BTN_UP)) != 0, IM_COL32(236, 194, 84, 180));
    draw_btn(dl, dcx, dcy + doff, dr, "v", (g_active_buttons & (1ull << BTN_DOWN)) != 0, IM_COL32(236, 194, 84, 180));
    draw_btn(dl, dcx - doff, dcy, dr, "<", (g_active_buttons & (1ull << BTN_LEFT)) != 0, IM_COL32(236, 194, 84, 180));
    draw_btn(dl, dcx + doff, dcy, dr, ">", (g_active_buttons & (1ull << BTN_RIGHT)) != 0, IM_COL32(236, 194, 84, 180));

    // 5. Shoulder buttons (L, ZL, R, ZR)
    float sh_w = 90.0f * s, sh_h = 38.0f * s;
    draw_capsule_btn(dl, (30.0f + g_safe_inset) * s, 30.0f * s, sh_w, sh_h, "L", (g_active_buttons & (1ull << BTN_L)) != 0);
    draw_capsule_btn(dl, (135.0f + g_safe_inset) * s, 25.0f * s, sh_w, sh_h, "ZL", (g_active_buttons & (1ull << BTN_ZL)) != 0);
    draw_capsule_btn(dl, W - (30.0f + g_safe_inset) * s - sh_w, 30.0f * s, sh_w, sh_h, "R", (g_active_buttons & (1ull << BTN_R)) != 0);
    draw_capsule_btn(dl, W - (135.0f + g_safe_inset) * s - sh_w, 25.0f * s, sh_w, sh_h, "ZR", (g_active_buttons & (1ull << BTN_ZR)) != 0);

    // 6. Utility (Minus, Menu, Plus)
    float ut_r = 22.0f * s;
    draw_btn(dl, W * 0.40f, 40.0f * s, ut_r, "-", (g_active_buttons & (1ull << BTN_MINUS)) != 0, IM_COL32(236, 194, 84, 180));
    draw_btn(dl, W * 0.60f, 40.0f * s, ut_r, "+", (g_active_buttons & (1ull << BTN_PLUS)) != 0, IM_COL32(236, 194, 84, 180));
    draw_btn(dl, W * 0.50f, 40.0f * s, ut_r, "M", false, IM_COL32(236, 194, 84, 180));
}
