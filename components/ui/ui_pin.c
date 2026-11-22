/* The settings PIN: a keypad popup, the lock in front of settings and
 * onboarding, and setting, changing or turning off the PIN.
 *
 * Unlocked lasts until the dashboard shows again (the back button, or the
 * screen blanking). Five wrong PINs in a row lock the keypad for a minute.
 * A forgotten PIN is cleared from the console ('pin off'). */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define PIN_LEN       4
#define MAX_TRIES     5
#define LOCKOUT_MS    60000

static struct {
    char digits[PIN_LEN + 1];
    int n;
    lv_obj_t *dot[PIN_LEN];
    lv_obj_t *hint;
    void (*done)(const char *pin);
} s_pad;

static bool s_unlocked;
static int s_wrong;
static uint32_t s_locked_until;
static void (*s_then)(void);

static void dots_show(void)
{
    for (int i = 0; i < PIN_LEN; i++) {
        lv_obj_set_style_bg_opa(s_pad.dot[i], i < s_pad.n ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

static void key_event(lv_event_t *e)
{
    const int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (k == -1) { /* backspace */
        if (s_pad.n > 0) {
            s_pad.digits[--s_pad.n] = '\0';
        }
        dots_show();
        return;
    }
    if (k == -2) { /* cancel */
        ui_popup_close();
        return;
    }
    if (s_pad.n >= PIN_LEN) {
        return;
    }
    s_pad.digits[s_pad.n++] = (char)('0' + k);
    s_pad.digits[s_pad.n] = '\0';
    dots_show();
    if (s_pad.n == PIN_LEN) {
        char pin[PIN_LEN + 1];
        memcpy(pin, s_pad.digits, sizeof pin);
        void (*done)(const char *) = s_pad.done;
        ui_popup_close();
        if (done) {
            done(pin);
        }
    }
}

void ui_pin_ask(const char *title, const char *hint, void (*done)(const char *pin))
{
    memset(&s_pad, 0, sizeof s_pad);
    s_pad.done = done;
    const int kw = 104, kh = 56;
    lv_obj_t *box = ui_popup(title, 3 * kw + 2 * GAP + 32, LV_SIZE_CONTENT, true);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_pad.hint = ui_label(box, F_BODY, C_MUTED, hint ? hint : "");
    lv_obj_t *dots = ui_row(box, 18);
    lv_obj_set_width(dots, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(dots, 8, 0);
    for (int i = 0; i < PIN_LEN; i++) {
        lv_obj_t *d = ui_box(dots);
        lv_obj_set_size(d, 20, 20);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(d, 2, 0);
        lv_obj_set_style_border_color(d, C_COPPER, 0);
        lv_obj_set_style_bg_color(d, C_COPPER, 0);
        s_pad.dot[i] = d;
    }
    dots_show();
    lv_obj_t *grid = ui_box(box);
    lv_obj_set_size(grid, 3 * kw + 2 * GAP, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(grid, GAP, 0);
    static const int keys[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, -2, 0, -1 };
    for (int i = 0; i < 12; i++) {
        char t[12];
        const char *txt = t;
        if (keys[i] == -1) {
            txt = HM_ICON_BKSP;
        } else if (keys[i] == -2) {
            txt = "Cancel";
        } else {
            snprintf(t, sizeof t, "%d", keys[i]);
        }
        lv_obj_t *b = ui_btn(grid, txt, key_event, (void *)(intptr_t)keys[i]);
        lv_obj_set_size(b, kw, kh);
        lv_obj_set_style_pad_hor(b, 0, 0);
        if (keys[i] >= 0) {
            lv_obj_set_style_text_font(ui_btn_label(b), F_TITLE, 0);
        } else if (keys[i] == -1) {
            lv_obj_set_style_text_font(ui_btn_label(b), &hm_icon_22, 0);
        }
    }
}

/* ---- the lock ------------------------------------------------------------------- */

static void unlock_entered(const char *pin)
{
    if (ui_platform_pin_check(pin)) {
        s_unlocked = true;
        s_wrong = 0;
        void (*then)(void) = s_then;
        s_then = NULL;
        if (then) {
            then();
        }
        return;
    }
    if (++s_wrong >= MAX_TRIES) {
        s_wrong = 0;
        s_locked_until = lv_tick_get() + LOCKOUT_MS;
        ui_toast("Too many wrong PINs. Try again in a minute.");
        return;
    }
    ui_pin_ask("Enter the PIN", "Wrong PIN, try again", unlock_entered);
}

bool ui_pin_unlocked(void)
{
    return s_unlocked || !ui_platform_pin_enabled();
}

void ui_with_pin(void (*then)(void))
{
    if (ui_pin_unlocked()) {
        s_unlocked = true;
        then();
        return;
    }
    if (s_locked_until && (int32_t)(lv_tick_get() - s_locked_until) < 0) {
        ui_toast("Too many wrong PINs. Try again in a minute.");
        return;
    }
    s_locked_until = 0;
    s_then = then;
    ui_pin_ask("Enter the PIN", "Settings are locked", unlock_entered);
}

void ui_pin_lock(void)
{
    s_unlocked = false;
}

/* ---- set, change or turn off ------------------------------------------------------ */

static char s_new[PIN_LEN + 1];
static void (*s_changed)(void);

static void confirm_entered(const char *pin)
{
    if (strcmp(pin, s_new) != 0) {
        ui_toast("The PINs did not match. The PIN is unchanged.");
        return;
    }
    if (ui_platform_pin_save(pin)) {
        ui_toast("PIN set. Settings and onboarding now ask for it.");
    } else {
        ui_toast("The PIN could not be saved");
    }
    if (s_changed) {
        s_changed();
    }
}

static void new_entered(const char *pin)
{
    memcpy(s_new, pin, sizeof s_new);
    ui_pin_ask("Confirm the PIN", "Enter it again", confirm_entered);
}

static void pin_pick(int i)
{
    if (i == 0) {
        ui_pin_ask("New PIN", "Four digits", new_entered);
        return;
    }
    if (ui_platform_pin_save(NULL)) {
        ui_toast("PIN turned off");
    }
    if (s_changed) {
        s_changed();
    }
}

void ui_pin_setup(void (*changed)(void))
{
    s_changed = changed;
    if (!ui_platform_pin_enabled()) {
        ui_pin_ask("New PIN", "Four digits for settings and onboarding", new_entered);
        return;
    }
    static const char *const opts[] = { "Change PIN", "Turn off PIN" };
    ui_choose("PIN", opts, 2, -1, pin_pick);
}
