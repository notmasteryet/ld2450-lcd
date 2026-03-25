#include "radar_ui.h"
#include "display.h"

#include "lvgl.h"
#include <stdio.h>

/*
 * Radar coordinate system
 * -----------------------
 * MODE_FRONT — sensor faces forward, mounted facing the user:
 *   Origin at top-centre. Targets spread downward.
 *   screen_x = SENSOR_X + x / MM_PER_PX
 *   screen_y = 0        + y / MM_PER_PX
 *   X mirrors user movement (move right → dot goes right).
 *
 * MODE_BACK — sensor faces backward, mounted on user's back:
 *   Origin at bottom-centre. Targets spread upward.
 *   screen_x = SENSOR_X - x / MM_PER_PX   (X flipped: real-right = screen-right)
 *   screen_y = LCD_V_RES - y / MM_PER_PX
 *
 * Scale: MM_PER_PX = 40 → rings at 2 m / 4 m / 6 m (50 / 100 / 150 px)
 * FOV:   LD2450 60° horizontal (±30°).
 *        spread = tan(30°) × LCD_V_RES = 0.5774 × 240 ≈ 139 px
 */

#define SENSOR_X     (LCD_H_RES / 2)   /* 160 px */
#define MM_PER_PX    33     /* 8000 mm / 240 px ≈ 33 — shows full 8 m range */
#define RING_STEP_MM 2000
#define RING_COUNT   4      /* rings at 2 / 4 / 6 / 8 m */
#define DOT_DIAM     10
#define FOV_SPREAD_PX 139   /* tan(30°) × 240 */

typedef enum { MODE_FRONT = 0, MODE_BACK = 1 } sensor_mode_t;
static sensor_mode_t s_mode = MODE_FRONT;

static const lv_color_t TARGET_COLORS[LD2450_MAX_TARGETS] = {
    LV_COLOR_MAKE(0xFF, 0x40, 0x40),
    LV_COLOR_MAKE(0x40, 0xFF, 0x40),
    LV_COLOR_MAKE(0x40, 0x80, 0xFF),
};

static lv_obj_t *s_dot[LD2450_MAX_TARGETS];
static lv_obj_t *s_info[LD2450_MAX_TARGETS];

/* Module-scoped point arrays — LVGL holds a pointer, so they must outlive the objects */
static lv_point_t s_vline_pts[2];
static lv_point_t s_fov_left_pts[2];
static lv_point_t s_fov_right_pts[2];

/* ---- helpers ---- */

static lv_obj_t *make_ring(lv_obj_t *parent, int cx, int cy,
                            int radius, lv_color_t color)
{
    int diam = radius * 2;
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, diam, diam);
    lv_obj_set_pos(obj, cx - radius, cy - radius);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *make_line(lv_obj_t *parent, lv_point_t *pts,
                            lv_color_t color, int width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, pts, 2);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_width(line, width, 0);
    return line;
}

static lv_obj_t *make_dot(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, DOT_DIAM, DOT_DIAM);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    return obj;
}

static void rebuild(void)
{
    /* Sensor screen position: top for FRONT, bottom for BACK */
    int origin_y = (s_mode == MODE_FRONT) ? 0 : LCD_V_RES;
    /* For label placement: rings go downward (FRONT) or upward (BACK) */
    int y_dir    = (s_mode == MODE_FRONT) ? 1 : -1;

    lv_color_t ring_color  = LV_COLOR_MAKE(0x00, 0x33, 0x00);
    lv_color_t fov_color   = LV_COLOR_MAKE(0x00, 0x55, 0x00);
    lv_color_t label_color = LV_COLOR_MAKE(0x00, 0x66, 0x00);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);

    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Distance rings centred on origin */
    for (int i = 1; i <= RING_COUNT; i++) {
        int r = (i * RING_STEP_MM) / MM_PER_PX;   /* 50, 100, 150 px */
        make_ring(scr, SENSOR_X, origin_y, r, ring_color);
    }

    /* Centre axis (full screen height) */
    s_vline_pts[0] = (lv_point_t){ SENSOR_X, 0 };
    s_vline_pts[1] = (lv_point_t){ SENSOR_X, LCD_V_RES };
    make_line(scr, s_vline_pts, ring_color, 1);

    /* 60° FOV lines from origin to opposite screen edge */
    int far_y = (s_mode == MODE_FRONT) ? LCD_V_RES : 0;
    s_fov_left_pts[0]  = (lv_point_t){ SENSOR_X,                  origin_y };
    s_fov_left_pts[1]  = (lv_point_t){ SENSOR_X - FOV_SPREAD_PX,  far_y    };
    s_fov_right_pts[0] = (lv_point_t){ SENSOR_X,                  origin_y };
    s_fov_right_pts[1] = (lv_point_t){ SENSOR_X + FOV_SPREAD_PX,  far_y    };
    make_line(scr, s_fov_left_pts,  fov_color, 1);
    make_line(scr, s_fov_right_pts, fov_color, 1);

    /* Distance labels along the centre axis */
    for (int i = 1; i <= RING_COUNT; i++) {
        int dist_mm  = i * RING_STEP_MM;
        int radius   = dist_mm / MM_PER_PX;
        int label_y  = origin_y + y_dir * radius;
        if (label_y < 2 || label_y >= LCD_V_RES - 10) continue;

        lv_obj_t *lbl = lv_label_create(scr);
        char buf[8];
        snprintf(buf, sizeof(buf), "%dm", dist_mm / 1000);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, label_color, 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
        lv_obj_set_pos(lbl, SENSOR_X + 3, label_y - 10);
    }

    /* Mode indicator (top-right corner) */
    lv_obj_t *mode_lbl = lv_label_create(scr);
    lv_label_set_text(mode_lbl, (s_mode == MODE_FRONT) ? "FWD" : "BWD");
    lv_obj_set_style_text_color(mode_lbl, label_color, 0);
    lv_obj_set_style_bg_opa(mode_lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(mode_lbl, LCD_H_RES - 28, 2);

    /* Target dots — on top */
    for (int i = 0; i < LD2450_MAX_TARGETS; i++) {
        s_dot[i] = make_dot(scr, TARGET_COLORS[i]);
    }

    /* Target info labels — top-left, one row per target */
    for (int i = 0; i < LD2450_MAX_TARGETS; i++) {
        s_info[i] = lv_label_create(scr);
        lv_label_set_text(s_info[i], "");
        lv_obj_set_style_text_color(s_info[i], TARGET_COLORS[i], 0);
        lv_obj_set_style_bg_color(s_info[i], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_info[i], LV_OPA_50, 0);
        lv_obj_set_pos(s_info[i], 2, i * 16);
    }
}

/* ---- public API ---- */

void radar_ui_init(void)
{
    rebuild();
}

void radar_ui_toggle_mode(void)
{
    s_mode = (s_mode == MODE_FRONT) ? MODE_BACK : MODE_FRONT;
    rebuild();
}

void radar_ui_update(const ld2450_frame_t *frame)
{
    int origin_y = (s_mode == MODE_FRONT) ? 0        : LCD_V_RES;
    int x_sign   = (s_mode == MODE_FRONT) ? 1        : -1;
    int y_sign   = (s_mode == MODE_FRONT) ? 1        : -1;

    for (int i = 0; i < LD2450_MAX_TARGETS; i++) {
        const ld2450_target_t *t = &frame->targets[i];

        if (!t->valid) {
            lv_obj_add_flag(s_dot[i], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_info[i], "");
            continue;
        }

        int sx = SENSOR_X + x_sign * (t->x / MM_PER_PX);
        int sy = origin_y + y_sign * (t->y / MM_PER_PX);

        if (sx >= 0 && sx < LCD_H_RES && sy >= 0 && sy < LCD_V_RES) {
            lv_obj_set_pos(s_dot[i], sx - DOT_DIAM / 2, sy - DOT_DIAM / 2);
            lv_obj_clear_flag(s_dot[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_dot[i], LV_OBJ_FLAG_HIDDEN);
        }

        char buf[32];
        snprintf(buf, sizeof(buf), "T%d X%+d Y%d V%+d",
                 i + 1, t->x, t->y, t->speed);
        lv_label_set_text(s_info[i], buf);
    }
}
