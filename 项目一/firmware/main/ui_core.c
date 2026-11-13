/* UI 实现（用户修正版：全界面统一 20px 字体，颜色分层保留）
 *
 * 布局（240×320，全部边距 ≥4px）：
 *   4..28    标题「液位控制站」白
 *   32..56   IP 行 浅灰
 *   60..260  主卡片 #333333（内边距 4；左水箱 100×144；右参数组 x=112）
 *   264..288  状态卡 #333333（●主站 ●已到位 液温）
 *   292..316  阀门行（出水阀 浅灰 + A+10%/B-10% 荧光青）
 *
 * 行高 24px（20px 字体实测），四组参数每行占 48px，卡片内 192px 恰好容纳。
 * 修复记录：上一版 OUT 流量行超出卡片底部被状态卡遮挡（断言脚本缺
 * "子控件必须在父容器内"检查，已补回）。
 */
#include "ui_core.h"
#include <stdio.h>
#include <string.h>

#define FILL_ALPHA 204   /* 液位填充 alpha=0.8 */

static lv_obj_t *s_lbl_ip;
static lv_obj_t *s_lbl_sp_val, *s_lbl_level_val;
static lv_obj_t *s_lbl_in_pct, *s_lbl_in_flow;
static lv_obj_t *s_lbl_out_pct, *s_lbl_out_flow;
static lv_obj_t *s_lbl_temp_val;
static lv_obj_t *s_lbl_valve_a, *s_lbl_valve_b;
static lv_obj_t *s_tank_fill;
static lv_obj_t *s_lamp_master, *s_lamp_atsp;
static int s_tank_inner_h;
static const lv_font_t *s_font;

static void style_label(lv_obj_t *l, lv_color_t c)
{
    lv_obj_set_style_text_font(l, s_font, 0);
    lv_obj_set_style_text_color(l, c, 0);
}

/* flex 行容器：自动避让子控件，杜绝硬编码偏移导致的重叠 */
static lv_obj_t *mk_flex_row(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, 112, 24);
    lv_obj_align(r, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(r, false);
    return r;
}

void ui_core_create(lv_obj_t *screen, const lv_font_t *font)
{
    s_font = font;
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_C_BG), 0);

    /* ---- 标题（白，居中） ---- */
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "液位控制站");
    style_label(title, lv_color_hex(UI_C_TITLE));
    lv_obj_set_width(title, 232);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 4);

    /* ---- IP 行（浅灰） ---- */
    s_lbl_ip = lv_label_create(screen);
    lv_label_set_text(s_lbl_ip, "IP ---:502");
    style_label(s_lbl_ip, lv_color_hex(UI_C_LABEL));
    lv_obj_align(s_lbl_ip, LV_ALIGN_TOP_LEFT, 4, 32);

    /* ---- 主卡片 ---- */
    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_set_size(card, 232, 200);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, 4, 60);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_C_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_scrollable(card, false);

    /* 左侧水箱（白描边，液位填充荧光青 alpha 0.8，上沿白线） */
    lv_obj_t *tank = lv_obj_create(card);
    lv_obj_set_size(tank, 100, 176);                  /* 加高并垂直居中（与右侧列视觉平衡） */
    lv_obj_align(tank, LV_ALIGN_TOP_LEFT, 8, 10);
    lv_obj_set_style_bg_opa(tank, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(tank, lv_color_hex(UI_C_TITLE), 0);
    lv_obj_set_style_border_width(tank, 2, 0);
    lv_obj_set_style_radius(tank, 14, 0);            /* 圆角容器 */
    lv_obj_set_style_pad_all(tank, 3, 0);
    /* 液面按罐形裁剪：LVGL 9 默认裁剪子控件（除非设 OVERFLOW_VISIBLE） */
    lv_obj_set_scrollable(tank, false);

    s_tank_inner_h = 176 - 6;
    s_tank_fill = lv_obj_create(tank);
    lv_obj_set_width(s_tank_fill, 100 - 6);          /* 比罐体窄 = 容器壁厚 */
    lv_obj_set_style_bg_color(s_tank_fill, lv_color_hex(UI_C_VALUE), 0);
    lv_obj_set_style_bg_opa(s_tank_fill, FILL_ALPHA, 0);
    lv_obj_set_style_radius(s_tank_fill, 0, 0);      /* 由罐形裁剪出圆角 */
    lv_obj_set_style_border_side(s_tank_fill, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(s_tank_fill, lv_color_hex(UI_C_TITLE), 0);
    lv_obj_set_style_border_width(s_tank_fill, 1, 0);
    lv_obj_align(s_tank_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_height(s_tank_fill, 0);

    /* 右侧参数组 x=112（统一 20px，标签浅灰/数值荧光青） */
    int cx = 112;
    /* 行 1：设定 */
    lv_obj_t *t_sp = lv_label_create(card);
    lv_label_set_text(t_sp, "设定");
    style_label(t_sp, lv_color_hex(UI_C_LABEL));
    lv_obj_align(t_sp, LV_ALIGN_TOP_LEFT, cx, 3);
    s_lbl_sp_val = lv_label_create(card);
    lv_label_set_text(s_lbl_sp_val, "250.0 mm");
    style_label(s_lbl_sp_val, lv_color_hex(UI_C_VALUE));
    lv_obj_align(s_lbl_sp_val, LV_ALIGN_TOP_LEFT, cx, 27);

    /* 行 2：实际 */
    lv_obj_t *t_lv = lv_label_create(card);
    lv_label_set_text(t_lv, "实际");
    style_label(t_lv, lv_color_hex(UI_C_LABEL));
    lv_obj_align(t_lv, LV_ALIGN_TOP_LEFT, cx, 51);
    s_lbl_level_val = lv_label_create(card);
    lv_label_set_text(s_lbl_level_val, "250.0 mm");
    style_label(s_lbl_level_val, lv_color_hex(UI_C_VALUE));
    lv_obj_align(s_lbl_level_val, LV_ALIGN_TOP_LEFT, cx, 75);

    /* 行 3/行 4：IN/OUT 用 flex 行（自动避让，杜绝重叠） */
    lv_obj_t *row_in = mk_flex_row(card, cx, 99);
    lv_obj_t *t_in = lv_label_create(row_in);
    lv_label_set_text(t_in, "IN");
    lv_obj_set_style_pad_all(t_in, 0, 0);
    style_label(t_in, lv_color_hex(UI_C_LABEL));
    s_lbl_in_pct = lv_label_create(row_in);
    lv_label_set_text(s_lbl_in_pct, "11%");
    lv_obj_set_style_pad_all(s_lbl_in_pct, 0, 0);
    style_label(s_lbl_in_pct, lv_color_hex(UI_C_VALUE));
    s_lbl_in_flow = lv_label_create(card);
    lv_label_set_text(s_lbl_in_flow, "1.14 L/min");
    style_label(s_lbl_in_flow, lv_color_hex(UI_C_VALUE));
    lv_obj_align(s_lbl_in_flow, LV_ALIGN_TOP_LEFT, cx, 123);

    lv_obj_t *row_out = mk_flex_row(card, cx, 147);
    lv_obj_t *t_out = lv_label_create(row_out);
    lv_label_set_text(t_out, "OUT");
    lv_obj_set_style_pad_all(t_out, 0, 0);
    style_label(t_out, lv_color_hex(UI_C_LABEL));
    s_lbl_out_pct = lv_label_create(row_out);
    lv_label_set_text(s_lbl_out_pct, "30%");
    lv_obj_set_style_pad_all(s_lbl_out_pct, 0, 0);
    style_label(s_lbl_out_pct, lv_color_hex(UI_C_VALUE));
    s_lbl_out_flow = lv_label_create(card);
    lv_label_set_text(s_lbl_out_flow, "0.85 L/min");
    style_label(s_lbl_out_flow, lv_color_hex(UI_C_VALUE));
    lv_obj_align(s_lbl_out_flow, LV_ALIGN_TOP_LEFT, cx, 171);

    /* ---- 状态卡（●主站 ●已到位 液温） ---- */
    lv_obj_t *scard = lv_obj_create(screen);
    lv_obj_set_size(scard, 232, 24);
    lv_obj_align(scard, LV_ALIGN_TOP_LEFT, 4, 264);
    lv_obj_set_style_bg_color(scard, lv_color_hex(UI_C_CARD), 0);
    lv_obj_set_style_border_color(scard, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_border_width(scard, 1, 0);
    lv_obj_set_style_pad_all(scard, 0, 0);
    lv_obj_set_style_pad_column(scard, 0, 0);   /* flex 行间距必须显式清零 */
    lv_obj_set_style_pad_row(scard, 0, 0);
    lv_obj_set_style_radius(scard, 8, 0);
    lv_obj_set_flex_flow(scard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scard, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(scard, false);

    s_lamp_master = lv_obj_create(scard);
    lv_obj_set_size(s_lamp_master, 10, 10);
    lv_obj_set_style_radius(s_lamp_master, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_lamp_master, 0, 0);

    lv_obj_t *t_master = lv_label_create(scard);
    lv_label_set_text(t_master, "主站");
    lv_obj_set_style_pad_all(t_master, 0, 0);
    style_label(t_master, lv_color_hex(UI_C_LABEL));

    s_lamp_atsp = lv_obj_create(scard);
    lv_obj_set_size(s_lamp_atsp, 10, 10);
    lv_obj_set_style_radius(s_lamp_atsp, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_lamp_atsp, 0, 0);

    lv_obj_t *t_atsp = lv_label_create(scard);
    lv_label_set_text(t_atsp, "已到位");
    lv_obj_set_style_pad_all(t_atsp, 0, 0);
    style_label(t_atsp, lv_color_hex(UI_C_LABEL));

    lv_obj_t *t_temp = lv_label_create(scard);
    lv_label_set_text(t_temp, "液温");
    lv_obj_set_style_pad_all(t_temp, 0, 0);
    style_label(t_temp, lv_color_hex(UI_C_LABEL));

    s_lbl_temp_val = lv_label_create(scard);
    lv_label_set_text(s_lbl_temp_val, "32.9℃");
    lv_obj_set_style_pad_all(s_lbl_temp_val, 0, 0);
    style_label(s_lbl_temp_val, lv_color_hex(UI_C_VALUE));

    /* ---- 阀门行（flex 自动避让） ---- */
    lv_obj_t *row_valve = lv_obj_create(screen);
    lv_obj_set_size(row_valve, 224, 24);
    lv_obj_align(row_valve, LV_ALIGN_TOP_LEFT, 8, 292);
    lv_obj_set_style_bg_opa(row_valve, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_valve, 0, 0);
    lv_obj_set_style_pad_all(row_valve, 0, 0);
    lv_obj_set_style_pad_column(row_valve, 8, 0);
    lv_obj_set_flex_flow(row_valve, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_valve, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row_valve, false);

    lv_obj_t *t_valve = lv_label_create(row_valve);
    lv_label_set_text(t_valve, "出水阀");
    lv_obj_set_style_pad_all(t_valve, 0, 0);
    style_label(t_valve, lv_color_hex(UI_C_LABEL));

    s_lbl_valve_a = lv_label_create(row_valve);
    lv_label_set_text(s_lbl_valve_a, "A+10%");
    lv_obj_set_style_pad_all(s_lbl_valve_a, 0, 0);
    style_label(s_lbl_valve_a, lv_color_hex(UI_C_VALUE));

    s_lbl_valve_b = lv_label_create(row_valve);
    lv_label_set_text(s_lbl_valve_b, "B-10%");
    lv_obj_set_style_pad_all(s_lbl_valve_b, 0, 0);
    style_label(s_lbl_valve_b, lv_color_hex(UI_C_VALUE));
}

/* ---- 数值渲染：数据无效时 --.-- 浅灰，有效时荧光青 ---- */
static void fmt_val(lv_obj_t *l, bool valid, const char *valid_fmt,
                    float v1, float v2)
{
    if (valid) {
        lv_label_set_text_fmt(l, valid_fmt, v1, v2);
        lv_obj_set_style_text_color(l, lv_color_hex(UI_C_VALUE), 0);
    } else {
        lv_label_set_text(l, "--.--");
        lv_obj_set_style_text_color(l, lv_color_hex(UI_C_LABEL), 0);
    }
}

static void set_lamp(lv_obj_t *lamp, int state)
{
    uint32_t c = (state == 0) ? UI_C_OK : (state == 1) ? UI_C_WAIT : UI_C_ERR;
    lv_obj_set_style_bg_color(lamp, lv_color_hex(c), 0);
}

void ui_core_update(const ui_snapshot_t *s)
{
    if (s == NULL) {
        return;
    }
    bool v = s->data_valid;

    float frac = (s->height_mm > 0.0f) ? s->level_mm / s->height_mm : 0.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    lv_obj_set_height(s_tank_fill, (lv_coord_t)(s_tank_inner_h * frac));

    fmt_val(s_lbl_sp_val, v, "%.1f mm", s->setpoint_mm, 0.0f);
    fmt_val(s_lbl_level_val, v, "%.1f mm", s->level_mm, 0.0f);
    fmt_val(s_lbl_in_pct, v, "%.0f%%", s->valve_in_pct, 0.0f);
    fmt_val(s_lbl_in_flow, v, "%.2f L/min", s->flow_in, 0.0f);
    fmt_val(s_lbl_out_pct, v, "%.0f%%", s->valve_out_pct, 0.0f);
    fmt_val(s_lbl_out_flow, v, "%.2f L/min", s->flow_out, 0.0f);
    fmt_val(s_lbl_temp_val, v, "%.1f℃", s->temp_c, 0.0f);

    lv_label_set_text_fmt(s_lbl_ip, "IP %s:502", s->ip_str);

    set_lamp(s_lamp_master, (s->status & UISTAT_MASTER_CONN) ? 0 : 2);
    set_lamp(s_lamp_atsp, (s->status & UISTAT_AT_SETPOINT) ? 0 : 1);
}

/* ---- 调试：对象树导出 ---- */
static const char *class_name(lv_obj_t *obj)
{
    if (lv_obj_check_type(obj, &lv_label_class)) return "label";
    if (lv_obj_check_type(obj, &lv_obj_class)) return "obj";
    return "?";
}

static void dump_obj(lv_obj_t *obj, int depth)
{
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const char *txt = lv_obj_check_type(obj, &lv_label_class)
                          ? lv_label_get_text(obj) : "-";
    const lv_font_t *f = lv_obj_check_type(obj, &lv_label_class)
                              ? lv_obj_get_style_text_font(obj, 0) : NULL;
    printf("OBJ depth=%d class=%s x=%d y=%d w=%d h=%d fh=%d text=[%s]\n",
           depth, class_name(obj),
           (int)a.x1, (int)a.y1,
           (int)(a.x2 - a.x1 + 1), (int)(a.y2 - a.y1 + 1),
           f ? (int)lv_font_get_line_height(f) : 0,
           txt ? txt : "");
    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; i++) {
        dump_obj(lv_obj_get_child(obj, i), depth + 1);
    }
}

void ui_core_dump_tree(void)
{
    dump_obj(lv_screen_active(), 0);
}
