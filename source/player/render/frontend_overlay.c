#include "player/render/internal.h"

#include <stdint.h>
#include <string.h>

#if defined(HAVE_SWITCH_EGL_GLES) && !defined(HAVE_MPV_RENDER_DK3D)
#include <GLES2/gl2.h>
#endif

#include "player/ui/layout.h"
#include "player/ui/overlay.h"

#define FRONTEND_OVERLAY_RECT_CAPACITY 24

typedef struct
{
    int x;
    int y;
    int width;
    int height;
    uint8_t gray;
} FrontendOverlayRect;

typedef void (*FrontendOverlayRectFillFn)(void *userdata, const FrontendOverlayRect *rect);

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static char frontend_font_normalize_char(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - 'a' + 'A');
    return c;
}

static const uint8_t *frontend_font_glyph_rows(char c)
{
    static const uint8_t glyph_space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t glyph_plus[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    static const uint8_t glyph_minus[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static const uint8_t glyph_dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06};
    static const uint8_t glyph_slash[7] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10};
    static const uint8_t glyph_colon[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    static const uint8_t glyph_percent[7] = {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13};
    static const uint8_t digits[10][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
        {0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
        {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
        {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},
    };
    static const uint8_t letters[26][7] = {
        {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
        {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
        {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0F},
        {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F},
        {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C},
        {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
        {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
        {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
        {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
        {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
        {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
        {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
        {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
        {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    };

    c = frontend_font_normalize_char(c);
    if (c >= '0' && c <= '9')
        return digits[c - '0'];
    if (c >= 'A' && c <= 'Z')
        return letters[c - 'A'];

    switch (c)
    {
    case '+':
        return glyph_plus;
    case '-':
        return glyph_minus;
    case '.':
        return glyph_dot;
    case '/':
        return glyph_slash;
    case ':':
        return glyph_colon;
    case '%':
        return glyph_percent;
    case ' ':
    default:
        return glyph_space;
    }
}

static int frontend_font_measure_text(const char *text, int scale)
{
    int width = 0;

    if (!text || scale <= 0)
        return 0;

    while (*text)
    {
        width += 6 * scale;
        ++text;
    }
    if (width > 0)
        width -= scale;
    return width;
}

static void frontend_overlay_draw_text(const char *text,
                                       int x,
                                       int y,
                                       int scale,
                                       int max_width,
                                       uint8_t gray,
                                       FrontendOverlayRectFillFn fill_rect,
                                       void *userdata)
{
    int pen_x = x;

    if (!text || !text[0] || scale <= 0 || max_width <= 0 || !fill_rect)
        return;

    while (*text)
    {
        const uint8_t *glyph = frontend_font_glyph_rows(*text);
        int glyph_width = 5 * scale;
        int row;

        if (pen_x + glyph_width > x + max_width)
            break;

        for (row = 0; row < 7; ++row)
        {
            uint8_t bits = glyph[row];
            int col = 0;

            while (col < 5)
            {
                int run_start;
                int run_length = 0;
                FrontendOverlayRect rect;

                while (col < 5 && ((bits >> (4 - col)) & 1U) == 0U)
                    ++col;
                if (col >= 5)
                    break;

                run_start = col;
                while (col < 5 && ((bits >> (4 - col)) & 1U) != 0U)
                {
                    ++run_length;
                    ++col;
                }

                rect.x = pen_x + run_start * scale;
                rect.y = y + row * scale;
                rect.width = run_length * scale;
                rect.height = scale;
                rect.gray = gray;
                fill_rect(userdata, &rect);
            }
        }

        pen_x += 6 * scale;
        ++text;
    }
}

static void frontend_overlay_draw_text_shadowed(const char *text,
                                                int x,
                                                int y,
                                                int scale,
                                                int max_width,
                                                uint8_t gray,
                                                FrontendOverlayRectFillFn fill_rect,
                                                void *userdata)
{
    int shadow_offset;

    if (!text || !text[0] || scale <= 0 || max_width <= 0 || !fill_rect)
        return;

    shadow_offset = clamp_int(scale / 2, 1, 3);
    frontend_overlay_draw_text(text,
                               x + shadow_offset,
                               y + shadow_offset,
                               scale,
                               max_width,
                               3,
                               fill_rect,
                               userdata);
    frontend_overlay_draw_text(text,
                               x,
                               y,
                               scale,
                               max_width,
                               gray,
                               fill_rect,
                               userdata);
}

static void frontend_overlay_push_rect(FrontendOverlayRect *rects, size_t rect_capacity, size_t *count, int x, int y, int width, int height, uint8_t gray)
{
    if (!rects || !count || *count >= rect_capacity || width <= 0 || height <= 0)
        return;

    rects[*count].x = x;
    rects[*count].y = y;
    rects[*count].width = width;
    rects[*count].height = height;
    rects[*count].gray = gray;
    ++(*count);
}

static bool frontend_overlay_title_has_prefix(const char *title, const char *prefix)
{
    if (!title || !prefix)
        return false;
    while (*prefix)
    {
        if (*title++ != *prefix++)
            return false;
    }
    return true;
}

static const char *frontend_overlay_seek_delta_label(const char *title)
{
    const char *label;

    if (!frontend_overlay_title_has_prefix(title, "SEEK "))
        return NULL;

    label = title + 5;
    if (label[0] != '+' && label[0] != '-')
        return NULL;
    return label;
}

static bool frontend_overlay_bar_has_focus(const PlayerUiOverlayBar *bar)
{
    if (!bar)
        return false;

    if (bar->state == PLAYER_STATE_PAUSED ||
        bar->state == PLAYER_STATE_BUFFERING ||
        bar->state == PLAYER_STATE_SEEKING ||
        bar->state == PLAYER_STATE_LOADING ||
        bar->state == PLAYER_STATE_ERROR)
    {
        return true;
    }

    return frontend_overlay_title_has_prefix(bar->title, "PAUSED") ||
           frontend_overlay_title_has_prefix(bar->title, "PLAYING") ||
           frontend_overlay_title_has_prefix(bar->title, "SEEK") ||
           frontend_overlay_title_has_prefix(bar->title, "VOLUME");
}

static void frontend_overlay_push_triangle_icon(FrontendOverlayRect *rects,
                                                size_t rect_capacity,
                                                size_t *count,
                                                int cx,
                                                int cy,
                                                int size,
                                                int direction,
                                                uint8_t gray)
{
    const int rows = 7;
    int row_h = clamp_int(size / rows, 5, 14);
    int max_w = (size * 7) / 10;
    int top_y = cy - (rows * row_h) / 2;
    int i;

    for (i = 0; i < rows; ++i)
    {
        int distance = i <= rows / 2 ? i : rows - 1 - i;
        int w = clamp_int(((distance + 1) * max_w) / ((rows / 2) + 1), row_h, max_w);
        int x = direction >= 0 ? cx - max_w / 2 : cx + max_w / 2 - w;
        int y = top_y + i * row_h;
        frontend_overlay_push_rect(rects, rect_capacity, count, x, y, w, row_h - 1, gray);
    }
}

static void frontend_overlay_push_pause_icon(FrontendOverlayRect *rects,
                                             size_t rect_capacity,
                                             size_t *count,
                                             int cx,
                                             int cy,
                                             int size,
                                             uint8_t gray)
{
    int bar_w = clamp_int(size / 5, 10, 18);
    int bar_h = clamp_int((size * 4) / 5, 48, 84);
    int gap = clamp_int(size / 7, 10, 18);
    int y = cy - bar_h / 2;

    frontend_overlay_push_rect(rects, rect_capacity, count, cx - gap - bar_w, y, bar_w, bar_h, gray);
    frontend_overlay_push_rect(rects, rect_capacity, count, cx + gap, y, bar_w, bar_h, gray);
}

static void frontend_overlay_push_volume_icon(FrontendOverlayRect *rects,
                                              size_t rect_capacity,
                                              size_t *count,
                                              int cx,
                                              int cy,
                                              int size,
                                              uint8_t gray)
{
    int bar_w = clamp_int(size / 10, 7, 12);
    int gap = clamp_int(size / 12, 6, 10);
    int base_y = cy + size / 3;
    int i;

    for (i = 0; i < 4; ++i)
    {
        int h = clamp_int((size * (i + 2)) / 8, 18, size);
        int x = cx - (bar_w * 2 + gap * 2) + i * (bar_w + gap);
        frontend_overlay_push_rect(rects, rect_capacity, count, x, base_y - h, bar_w, h, gray);
    }
}

static void frontend_overlay_push_focus_icon(FrontendOverlayRect *rects,
                                             size_t rect_capacity,
                                             size_t *count,
                                             const PlayerUiOverlayBar *bar,
                                             int width,
                                             int height)
{
    int cx = width / 2;
    int cy = height / 2;
    int size = clamp_int(height / 8, 72, 104);

    if (!bar)
        return;

    if (frontend_overlay_title_has_prefix(bar->title, "SEEK"))
        return;

    if (frontend_overlay_title_has_prefix(bar->title, "VOLUME"))
    {
        frontend_overlay_push_volume_icon(rects, rect_capacity, count, cx, cy, size, 226);
        return;
    }

    if (frontend_overlay_title_has_prefix(bar->title, "PLAYING"))
    {
        frontend_overlay_push_pause_icon(rects, rect_capacity, count, cx, cy, size, 236);
        return;
    }

    frontend_overlay_push_triangle_icon(rects, rect_capacity, count, cx, cy, size, 1, 236);
}

static bool frontend_overlay_build_rects(const ViewContext *ctx, FrontendOverlayRect *rects, size_t rect_capacity, size_t *out_count)
{
    PlayerUiOverlaySnapshot overlay;
    int width;
    int height;
    int pad_x;
    int bottom_y;
    int bottom_height;
    int progress_height;
    int progress_y;
    int progress_width;
    int progress_fill_width;
    int progress_knob_x;
    int bubble_width;
    int bubble_height;
    int bubble_x;
    int bubble_y;
    PlayerUiLayout layout;
    size_t count = 0;

    if (out_count)
        *out_count = 0;
    if (!ctx || !player_ui_overlay_get_snapshot(&overlay) || overlay.kind == PLAYER_UI_OVERLAY_NONE)
        return false;

    width = (int)ctx->status.display_width;
    height = (int)ctx->status.display_height;
    if (width <= 0 || height <= 0)
        return false;

    if (overlay.kind == PLAYER_UI_OVERLAY_MESSAGE)
    {
        bubble_width = clamp_int(width / 3, 280, width - 96);
        bubble_height = clamp_int(height / 8, 72, 118);
        bubble_x = (width - bubble_width) / 2;
        bubble_y = (height - bubble_height) / 2;
        frontend_overlay_push_rect(rects, rect_capacity, &count, bubble_x, bubble_y, bubble_width, bubble_height, 16);
        frontend_overlay_push_rect(rects, rect_capacity, &count, bubble_x, bubble_y, bubble_width, 2, 78);
        frontend_overlay_push_rect(rects, rect_capacity, &count, bubble_x + 18, bubble_y + bubble_height - 10, bubble_width - 36, 2, 42);
        if (out_count)
            *out_count = count;
        return count > 0;
    }

    if (!player_ui_layout_compute(width, height, &layout))
        return false;
    pad_x = layout.pad_x;
    bottom_height = layout.bottom_height;
    bottom_y = layout.bottom_y;
    progress_height = layout.progress_height;
    progress_y = layout.progress_y;
    progress_width = layout.progress_width;
    progress_fill_width = clamp_int((overlay.bar.progress_permille * progress_width + 500) / 1000, 0, progress_width);
    progress_knob_x = layout.progress_x + progress_fill_width - progress_height;

    frontend_overlay_push_rect(rects, rect_capacity, &count, 0, bottom_y, width, bottom_height, 12);
    frontend_overlay_push_rect(rects, rect_capacity, &count, 0, bottom_y, width, 2, 42);
    frontend_overlay_push_rect(rects, rect_capacity, &count, layout.progress_x, progress_y, progress_width, progress_height, 58);
    if (progress_fill_width > 0)
        frontend_overlay_push_rect(rects, rect_capacity, &count, layout.progress_x, progress_y, progress_fill_width, progress_height, 218);
    progress_knob_x = clamp_int(progress_knob_x, layout.progress_x, layout.progress_x + progress_width - progress_height * 2);
    frontend_overlay_push_rect(rects, rect_capacity, &count, progress_knob_x, progress_y - progress_height / 2, progress_height * 2, progress_height * 2, 236);
    frontend_overlay_push_rect(rects, rect_capacity, &count, pad_x, progress_y + progress_height + 20, 190, 34, 25);
    frontend_overlay_push_rect(rects, rect_capacity, &count, width - pad_x - 150, progress_y + progress_height + 20, 150, 34, 25);

    if (frontend_overlay_bar_has_focus(&overlay.bar))
        frontend_overlay_push_focus_icon(rects, rect_capacity, &count, &overlay.bar, width, height);

    if (out_count)
        *out_count = count;
    return count > 0;
}

static void frontend_overlay_fill_rect_sw(void *pixels, size_t stride, u32 width, u32 height, const FrontendOverlayRect *rect)
{
    uint8_t *base = (uint8_t *)pixels;
    int x0;
    int y0;
    int x1;
    int y1;
    int y;

    if (!base || !rect || rect->width <= 0 || rect->height <= 0)
        return;

    x0 = clamp_int(rect->x, 0, (int)width);
    y0 = clamp_int(rect->y, 0, (int)height);
    x1 = clamp_int(rect->x + rect->width, 0, (int)width);
    y1 = clamp_int(rect->y + rect->height, 0, (int)height);
    if (x1 <= x0 || y1 <= y0)
        return;

    for (y = y0; y < y1; ++y)
    {
        uint8_t *row = base + (size_t)y * stride + (size_t)x0 * 4U;
        int x;

        for (x = x0; x < x1; ++x)
        {
            row[0] = rect->gray;
            row[1] = rect->gray;
            row[2] = rect->gray;
            row[3] = 0xFF;
            row += 4;
        }
    }
}

typedef struct
{
    void *pixels;
    size_t stride;
    u32 width;
    u32 height;
} FrontendOverlaySwTarget;

static void frontend_overlay_fill_rect_sw_cb(void *userdata, const FrontendOverlayRect *rect)
{
    FrontendOverlaySwTarget *target = (FrontendOverlaySwTarget *)userdata;

    if (!target)
        return;
    frontend_overlay_fill_rect_sw(target->pixels, target->stride, target->width, target->height, rect);
}

static void frontend_overlay_render_text_generic(const ViewContext *ctx, FrontendOverlayRectFillFn fill_rect, void *userdata)
{
    PlayerUiOverlaySnapshot overlay;
    int width;
    int height;
    int pad_x;
    int bottom_y;
    int title_scale;
    int hint_scale;
    int progress_y;
    int progress_height;
    int controls_y;
    int right_text_width;
    int center_text_width;
    int title_text_width;
    int bubble_width;
    int bubble_height;
    int bubble_x;
    int bubble_y;
    PlayerUiLayout layout;

    if (!ctx || !fill_rect || !player_ui_overlay_get_snapshot(&overlay) || overlay.kind == PLAYER_UI_OVERLAY_NONE)
        return;

    width = (int)ctx->status.display_width;
    height = (int)ctx->status.display_height;
    if (width <= 0 || height <= 0)
        return;

    if (overlay.kind == PLAYER_UI_OVERLAY_MESSAGE)
    {
        title_scale = clamp_int(height / 180, 3, 5);
        hint_scale = clamp_int(height / 300, 2, 3);
        bubble_width = clamp_int(width / 3, 280, width - 96);
        bubble_height = clamp_int(height / 8, 72, 118);
        bubble_x = (width - bubble_width) / 2;
        bubble_y = (height - bubble_height) / 2;
        title_text_width = frontend_font_measure_text(overlay.message.title, title_scale);
        center_text_width = frontend_font_measure_text(overlay.message.line1, hint_scale);

        frontend_overlay_draw_text_shadowed(overlay.message.title,
                                            bubble_x + (bubble_width - title_text_width) / 2,
                                            bubble_y + clamp_int(bubble_height / 4, 18, 28),
                                            title_scale,
                                            bubble_width - 48,
                                            236,
                                            fill_rect,
                                            userdata);
        frontend_overlay_draw_text_shadowed(overlay.message.line1,
                                            bubble_x + (bubble_width - center_text_width) / 2,
                                            bubble_y + bubble_height - clamp_int(bubble_height / 3, 24, 38),
                                            hint_scale,
                                            bubble_width - 48,
                                            176,
                                            fill_rect,
                                            userdata);
        return;
    }

    if (!player_ui_layout_compute(width, height, &layout))
        return;
    pad_x = layout.pad_x;
    bottom_y = layout.bottom_y;
    progress_height = layout.progress_height;
    progress_y = layout.progress_y;
    controls_y = progress_y + progress_height + 28;
    title_scale = clamp_int(height / 240, 2, 3);
    hint_scale = clamp_int(height / 330, 2, 2);

    frontend_overlay_draw_text_shadowed(overlay.bar.subtitle,
                                        pad_x,
                                        bottom_y + 18,
                                        hint_scale,
                                        width / 2,
                                        136,
                                        fill_rect,
                                        userdata);

    center_text_width = frontend_font_measure_text(overlay.bar.center, title_scale);
    frontend_overlay_draw_text_shadowed(overlay.bar.center,
                                        width - pad_x - center_text_width,
                                        bottom_y + 16,
                                        title_scale,
                                        width / 2,
                                        222,
                                        fill_rect,
                                        userdata);

    frontend_overlay_draw_text_shadowed(overlay.bar.left,
                                        pad_x + 18,
                                        controls_y + 8,
                                        hint_scale,
                                        170,
                                        222,
                                        fill_rect,
                                        userdata);

    right_text_width = frontend_font_measure_text(overlay.bar.right, hint_scale);
    frontend_overlay_draw_text_shadowed(overlay.bar.right,
                                        width - pad_x - 18 - right_text_width,
                                        controls_y + 8,
                                        hint_scale,
                                        132,
                                        222,
                                        fill_rect,
                                        userdata);

    frontend_overlay_draw_text_shadowed(overlay.bar.hint,
                                        pad_x + 220,
                                        controls_y + 8,
                                        hint_scale,
                                        width - pad_x * 2 - 420,
                                        152,
                                        fill_rect,
                                        userdata);

    if (frontend_overlay_title_has_prefix(overlay.bar.title, "SEEK"))
    {
        const char *seek_label = frontend_overlay_seek_delta_label(overlay.bar.title);
        const char *focus_text = seek_label ? seek_label : overlay.bar.center;
        int focus_scale = clamp_int(height / 170, 3, 4);
        int focus_width = frontend_font_measure_text(focus_text, focus_scale);
        frontend_overlay_draw_text_shadowed(focus_text,
                                            (width - focus_width) / 2,
                                            height / 2 - (7 * focus_scale) / 2,
                                            focus_scale,
                                            width - pad_x * 2,
                                            238,
                                            fill_rect,
                                            userdata);
    }
}

void frontend_overlay_render_sw(void *pixels, size_t stride, u32 width, u32 height, const ViewContext *ctx)
{
    FrontendOverlayRect rects[FRONTEND_OVERLAY_RECT_CAPACITY];
    size_t rect_count = 0;
    size_t i;
    FrontendOverlaySwTarget target;

    if (!pixels || !ctx || !frontend_overlay_build_rects(ctx, rects, sizeof(rects) / sizeof(rects[0]), &rect_count) || rect_count == 0)
        return;

    for (i = 0; i < rect_count; ++i)
        frontend_overlay_fill_rect_sw(pixels, stride, width, height, &rects[i]);

    target.pixels = pixels;
    target.stride = stride;
    target.width = width;
    target.height = height;
    frontend_overlay_render_text_generic(ctx, frontend_overlay_fill_rect_sw_cb, &target);
}


#if defined(HAVE_SWITCH_EGL_GLES) && !defined(HAVE_MPV_RENDER_DK3D)
static void frontend_gl_fill_rect(u32 display_height, const FrontendOverlayRect *rect)
{
    GLint y;
    float shade;

    if (!rect || rect->width <= 0 || rect->height <= 0)
        return;

    y = (GLint)display_height - (GLint)(rect->y + rect->height);
    shade = (float)rect->gray / 255.0f;
    glScissor(rect->x, y, rect->width, rect->height);
    glClearColor(shade, shade, shade, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void frontend_overlay_fill_rect_gl_cb(void *userdata, const FrontendOverlayRect *rect)
{
    u32 *display_height = (u32 *)userdata;

    if (!display_height)
        return;
    frontend_gl_fill_rect(*display_height, rect);
}

void frontend_overlay_render_gl(ViewContext *ctx)
{
    FrontendOverlayRect rects[FRONTEND_OVERLAY_RECT_CAPACITY];
    size_t rect_count = 0;
    size_t i;

    if (!ctx || !frontend_overlay_build_rects(ctx, rects, sizeof(rects) / sizeof(rects[0]), &rect_count) || rect_count == 0)
        return;

    glEnable(GL_SCISSOR_TEST);
    for (i = 0; i < rect_count; ++i)
        frontend_gl_fill_rect(ctx->status.display_height, &rects[i]);
    frontend_overlay_render_text_generic(ctx, frontend_overlay_fill_rect_gl_cb, &ctx->status.display_height);
    glDisable(GL_SCISSOR_TEST);
}

#endif

#ifdef HAVE_MPV_RENDER_DK3D
static void frontend_dk3d_fill_rect(DkCmdBuf cmdbuf, const FrontendOverlayRect *rect)
{
    DkScissor scissor;
    float shade;

    if (!rect || rect->width <= 0 || rect->height <= 0)
        return;

    scissor.x = (uint32_t)rect->x;
    scissor.y = (uint32_t)rect->y;
    scissor.width = (uint32_t)rect->width;
    scissor.height = (uint32_t)rect->height;
    shade = (float)rect->gray / 255.0f;
    dkCmdBufSetScissors(cmdbuf, 0, &scissor, 1);
    dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, shade, shade, shade, 1.0f);
}

static void frontend_overlay_fill_rect_dk3d_cb(void *userdata, const FrontendOverlayRect *rect)
{
    DkCmdBuf cmdbuf = userdata ? *(DkCmdBuf *)userdata : NULL;

    if (!cmdbuf)
        return;
    frontend_dk3d_fill_rect(cmdbuf, rect);
}

void frontend_overlay_render_dk3d(ViewContext *ctx, int slot)
{
    FrontendOverlayRect rects[FRONTEND_OVERLAY_RECT_CAPACITY];
    size_t rect_count = 0;
    size_t i;
    DkImageView image_view;
    DkViewport viewport;

    if (!ctx || !ctx->dk3d_overlay_cmdbuf || slot < 0 || slot >= FRONTEND_DK3D_FRAMEBUFFER_COUNT)
        return;
    if (!frontend_overlay_build_rects(ctx, rects, sizeof(rects) / sizeof(rects[0]), &rect_count) || rect_count == 0)
        return;

    memset(&viewport, 0, sizeof(viewport));
    viewport.width = (float)ctx->status.display_width;
    viewport.height = (float)ctx->status.display_height;
    viewport.near = 0.0f;
    viewport.far = 1.0f;

    dkCmdBufClear(ctx->dk3d_overlay_cmdbuf);
    dkImageViewDefaults(&image_view, &ctx->dk3d_framebuffers[slot]);
    dkCmdBufBindRenderTarget(ctx->dk3d_overlay_cmdbuf, &image_view, NULL);
    dkCmdBufSetViewports(ctx->dk3d_overlay_cmdbuf, 0, &viewport, 1);

    for (i = 0; i < rect_count; ++i)
        frontend_dk3d_fill_rect(ctx->dk3d_overlay_cmdbuf, &rects[i]);
    frontend_overlay_render_text_generic(ctx, frontend_overlay_fill_rect_dk3d_cb, &ctx->dk3d_overlay_cmdbuf);

    dkQueueSubmitCommands(ctx->dk3d_queue, dkCmdBufFinishList(ctx->dk3d_overlay_cmdbuf));
}
#endif
