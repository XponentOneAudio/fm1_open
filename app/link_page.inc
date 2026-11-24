/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Workshop Link page, included by screen.c. HOME+ARP opens it; HOME leaves.
 * Widgets come from a Workshop Computer card (workshop_link.c). Like the
 * other pages it draws from a per-frame snapshot, first to mark changed
 * areas, then once per LCD band, so each widget repaints only on change. */
static struct groove_link_view frame_link;
enum { LINK_TOP = 32, LINK_BOTTOM = 224 };
static const uint16_t link_colors[WL_C_COUNT] = {
    TEXT, ACCENT, DIM, GREEN, AMBER, 0xf8a8 /* red */, 0x3c9f /* blue */, 0xac9f /* violet */};

/* Text clipped to a widget's width rather than the screen's. */
static void link_text(unsigned x, unsigned y, const char *s, unsigned width, unsigned scale,
                      uint16_t color)
{
    char clipped[WL_MAX_TEXT + 1];
    unsigned n = 0, fit = width / (6U * scale);
    while (s[n] && n < fit && n < WL_MAX_TEXT) { clipped[n] = s[n]; ++n; }
    clipped[n] = 0;
    if (n) text(x, y, clipped, scale, color, BACK);
}

static void link_widget(const struct groove_link_view *v, const struct groove_link_widget *w)
{
    unsigned x = w->x, y = w->y < LINK_TOP ? LINK_TOP : w->y, width = w->w, height = w->h;
    if (x >= FM1_SCREEN_WIDTH || y >= LINK_BOTTOM || !width || !height) return;
    if (width > FM1_SCREEN_WIDTH - x) width = FM1_SCREEN_WIDTH - x;
    if (height > LINK_BOTTOM - y) height = LINK_BOTTOM - y;
    uint16_t color = w->color < WL_C_COUNT ? link_colors[w->color] : TEXT;
    paint_fill(x, y, width, height, BACK);
    char value[12];
    decimal(value, w->value, 1);
    unsigned value_width = 6U * (unsigned)strlen(value);
    switch (w->type) {
    case WL_W_LABEL:
        link_text(x, y, w->label, width, height >= 14 ? 2 : 1, color);
        break;
    case WL_W_VALUE:
        link_text(x, y, w->label, width, 1, DIM);
        if (height >= 24) link_text(x, y + 10, value, width, 2, color);
        else if (value_width <= width) text(x + width - value_width, y, value, 1, color, BACK);
        break;
    case WL_W_BAR: {
        link_text(x, y, w->label, width > value_width + 6 ? width - value_width - 6 : 0, 1, DIM);
        if (value_width <= width) text(x + width - value_width, y, value, 1, TEXT, BACK);
        if (height <= 10) break;
        unsigned bar = 0;
        if (w->max > w->min) {
            int span = w->max - w->min, at = w->value - w->min;
            at = at < 0 ? 0 : at > span ? span : at;
            bar = (unsigned)((long)at * (long)width / span);
        }
        paint_fill(x, y + 10, width, height - 10, PANEL);
        paint_fill(x, y + 10, bar, height - 10, color);
        break;
    }
    case WL_W_LED: {
        unsigned on = w->value != 0, size = height < 9 ? height : 9;
        paint_fill(x, y, size, size, on ? color : PANEL);
        if (width > size + 4) link_text(x + size + 4, y + (size > 7 ? (size - 7) / 2 : 0), w->label,
                                        width - size - 4, 1, on ? TEXT : DIM);
        break;
    }
    case WL_W_TEXT:
        if (height >= 17) {
            link_text(x, y, w->label, width, 1, DIM);
            link_text(x, y + 10, w->text, width, 1, color);
        } else link_text(x, y, w->text, width, 1, color);
        break;
    case WL_W_SCOPE: {
        if (width < 4 || height < 6) break;
        frame(x, y, width, height, PANEL);
        unsigned middle = y + height / 2, half = height / 2 - 2;
        paint_fill(x + 1, middle, width - 2, 1, PANEL);
        unsigned slot = w->scope, n = slot < WL_MAX_SCOPES ? v->scope_points[slot] : 0;
        if (n >= 2) {
            const int8_t *points = v->scope[slot];
            int last_x = 0, last_y = 0;
            for (unsigned i = 0; i < n; ++i) {
                int px = (int)(x + 1U + i * (width - 3U) / (n - 1U));
                int py = (int)middle - points[i] * (int)half / 128;
                if (i) line(last_x, last_y, px, py, color);
                last_x = px; last_y = py;
            }
        }
        link_text(x + 3, y + 3, w->label, width - 6, 1, DIM);
        break;
    }
    default:
        break;
    }
}

static __attribute__((noinline)) void link_draw(void)
{
    const struct groove_link_view *v = &frame_link;
    uint32_t sig = signature(v->revision * 2U + v->active, v->title, (unsigned)strlen(v->title));
    if (cache->title_cache != sig) {
        cache->title_cache = sig;
        cache->hint_cache = ~0U;
        memset(cache->tile_cache, 0xff, sizeof cache->tile_cache);
        paint_fill(0, 0, 240, 240, BACK);
        paint_fill(0, 29, 240, 1, PANEL);
        text(8, 8, v->title[0] ? v->title : "WORKSHOP LINK", 2, ACCENT, BACK);
    }
    if (!v->count) {
        radio_line(0, 60, v->active ? "CONNECTED - NO WIDGETS" : "WAITING FOR WORKSHOP COMPUTER", v->active ? GREEN : AMBER);
        radio_line(1, 84, "CONNECT FM1 USB TO THE COMPUTER", DIM);
        radio_line(2, 96, "RUN A WORKSHOP LINK CARD", DIM);
    }
    for (unsigned i = 0; i < v->count && i < WL_MAX_WIDGETS; ++i) {
        const struct groove_link_widget *w = &v->widget[i];
        uint32_t s = signature(i * 2U + v->active, w, sizeof *w);
        if (w->type == WL_W_SCOPE && w->scope < WL_MAX_SCOPES)
            s = signature(s + v->scope_points[w->scope], v->scope[w->scope], v->scope_points[w->scope]);
        if (cache->tile_cache[i] == s) continue;
        cache->tile_cache[i] = s;
        link_widget(v, w);
    }
    char status[48];
    const char *who = v->peer[0] ? v->peer : "COMPUTER";
    unsigned n = (unsigned)strlen(who);
    memcpy(status, who, n);
    strcpy(status + n, v->active ? "  HOME EXIT" : (v->count ? "  LINK LOST" : "  HOME EXIT"));
    uint32_t hint = signature(v->active, status, (unsigned)strlen(status));
    if (cache->hint_cache != hint) {
        cache->hint_cache = hint;
        paint_fill(8, 229, 224, 7, BACK);
        text(8, 229, v->count || v->active ? status : "HOME EXIT", 1, v->active ? DIM : AMBER, BACK);
    }
}
