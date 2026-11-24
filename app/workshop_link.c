/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Workshop Link device side. The routing task only copies complete frames
 * into a small single-producer/single-consumer inbox; all decoding, state
 * and replies happen on the control task. Nothing here touches Flash, audio
 * or the engine, so a link session cannot disturb playback. */
#include "workshop_link.h"
#include <string.h>
#ifdef FM1_HOST_TEST
#include <stdatomic.h>
#define LINK_WORD _Atomic(unsigned)
#else
#define LINK_WORD volatile unsigned
#endif

enum { INBOX = 8 }; /* power of two */
static struct {
    uint8_t bytes[WL_MAX_FRAME];
    uint8_t length, source;
} inbox[INBOX];
static LINK_WORD inbox_read, inbox_write, inbox_dropped;

static struct groove_link_view view;
static unsigned port, show_request, visible;
static uint32_t last_frame;
static uint8_t sequence, ack_pending, need_layout_pending;
static unsigned event_dirty;
static uint16_t event_buttons;
static int event_encoders[7];

static void barrier(void)
{
#ifndef FM1_HOST_TEST
    __asm__ volatile("csync" ::: "memory");
#endif
}

int groove_link_sysex(const uint8_t *m, size_t n)
{
    if (!wl_is_link_frame(m, n)) return 0;
    unsigned w = inbox_write;
    if (n > WL_MAX_FRAME || w - inbox_read >= INBOX) {
        ++inbox_dropped;
        return 1;
    }
    memcpy(inbox[w % INBOX].bytes, m, n);
    inbox[w % INBOX].length = (uint8_t)n;
    inbox[w % INBOX].source = (uint8_t)fm1_midi_input_source();
    barrier();
    inbox_write = w + 1;
    fm1_control_request();
    return 1;
}

static int send(unsigned command, const uint8_t *payload, size_t bytes)
{
    uint8_t frame[WL_MAX_FRAME];
    size_t length = wl_encode(frame, sizeof frame, command, sequence, payload, bytes);
    if (!length) return -1;
    int r = fm1_midi_send_port(port, frame, length);
    if (!r) sequence = (uint8_t)((sequence + 1U) & 0x7fU);
    return r;
}

static void clear_layout(void)
{
    view.count = 0;
    view.title[0] = 0;
    memset(view.widget, 0, sizeof view.widget);
    memset(view.scope_points, 0, sizeof view.scope_points);
    ++view.revision;
}

static void widget(struct wl_reader *r)
{
    unsigned id = wl_get8(r);
    struct groove_link_widget w;
    memset(&w, 0, sizeof w);
    w.type = (uint8_t)wl_get8(r);
    w.x = (uint8_t)wl_get8(r); w.y = (uint8_t)wl_get8(r);
    w.w = (uint8_t)wl_get8(r); w.h = (uint8_t)wl_get8(r);
    w.color = (uint8_t)wl_get8(r);
    w.min = (int16_t)wl_get16(r); w.max = (int16_t)wl_get16(r);
    wl_get_str(r, w.label, sizeof w.label);
    if (!r->ok || id >= WL_MAX_WIDGETS || w.type >= WL_W_COUNT) return;
    w.scope = 0xff;
    if (w.type == WL_W_SCOPE) {
        /* Reuse this id's slot, else take the first one no widget holds. */
        unsigned used = 0;
        for (unsigned i = 0; i < view.count; ++i)
            if (i != id && view.widget[i].type == WL_W_SCOPE && view.widget[i].scope < WL_MAX_SCOPES)
                used |= 1U << view.widget[i].scope;
        for (unsigned s = 0; s < WL_MAX_SCOPES; ++s)
            if (!(used & (1U << s))) { w.scope = (uint8_t)s; break; }
        if (w.scope == 0xff) w.type = WL_W_LABEL; /* no slot left: still show its label */
        else view.scope_points[w.scope] = 0;
    }
    view.widget[id] = w;
    if (id >= view.count) view.count = (uint8_t)(id + 1U);
    ++view.revision;
}

static void scope(struct wl_reader *r)
{
    unsigned id = wl_get8(r), offset = wl_get8(r), count = wl_get8(r);
    if (!r->ok || id >= view.count || view.widget[id].type != WL_W_SCOPE) return;
    unsigned slot = view.widget[id].scope;
    if (slot >= WL_MAX_SCOPES || offset >= WL_MAX_SCOPE_POINTS) return;
    if (count > WL_MAX_SCOPE_POINTS - offset) count = WL_MAX_SCOPE_POINTS - offset;
    if (count > wl_remaining(r)) return;
    for (unsigned i = 0; i < count; ++i)
        view.scope[slot][offset + i] = (int8_t)wl_get8(r);
    /* A frame starting at 0 begins a new trace; later frames extend it. */
    if (!offset || offset + count > view.scope_points[slot])
        view.scope_points[slot] = (uint8_t)(offset + count);
}

static void receive(const uint8_t *m, size_t n, unsigned source, uint32_t now)
{
    uint8_t payload[WL_MAX_PAYLOAD], command;
    int bytes = wl_decode(m, n, &command, NULL, payload, sizeof payload);
    if (bytes < 0) return;
    struct wl_reader r;
    wl_reader_init(&r, payload, (size_t)bytes);
    port = source;
    last_frame = now;
    if (command == WL_HELLO) {
        wl_get8(&r); /* host version; wl_decode has checked the frame version */
        unsigned flags = wl_get8(&r);
        clear_layout();
        wl_get_str(&r, view.peer, sizeof view.peer);
        view.active = 1;
        ack_pending = 1;
        need_layout_pending = 0;
        if (flags & WL_HELLO_SHOW) show_request = 2;
        return;
    }
    if (command == WL_BYE) { view.active = 0; return; }
    if (!view.active) {
        /* An update without a session (this firmware restarted, or the link
         * timed out): ask the host to start again. */
        if (command < WL_HOST_USER_FIRST || command > WL_HOST_USER_LAST) need_layout_pending = 1;
        return;
    }
    switch (command) {
    case WL_LAYOUT:
        clear_layout();
        wl_get_str(&r, view.title, sizeof view.title);
        break;
    case WL_WIDGET:
        widget(&r);
        break;
    case WL_VALUES:
        while (wl_remaining(&r) >= 3U) {
            unsigned id = wl_get8(&r);
            int value = wl_get16(&r);
            if (id < view.count) view.widget[id].value = (int16_t)value;
        }
        break;
    case WL_TEXT: {
        unsigned id = wl_get8(&r);
        char text[WL_MAX_TEXT + 1];
        wl_get_str(&r, text, sizeof text);
        if (r.ok && id < view.count) memcpy(view.widget[id].text, text, sizeof text);
        break;
    }
    case WL_SCOPE:
        scope(&r);
        break;
    case WL_SHOW:
        show_request = wl_get8(&r) ? 2 : 1;
        break;
    default:
        /* WL_PING keeps the session alive. Card-specific commands
         * (WL_HOST_USER_FIRST..LAST) are the place for firmware extensions. */
        break;
    }
}

static void flush(void)
{
    if (ack_pending) {
        uint8_t p[8 + WL_MAX_NAME];
        struct wl_writer w;
        wl_writer_init(&w, p, sizeof p);
        wl_put8(&w, WL_VERSION);
        wl_put8(&w, WL_CAP_SCREEN | WL_CAP_ENCODERS | WL_CAP_BUTTONS);
        wl_put8(&w, FM1_SCREEN_WIDTH); wl_put8(&w, FM1_SCREEN_HEIGHT);
        wl_put8(&w, WL_MAX_WIDGETS); wl_put8(&w, WL_MAX_SCOPE_POINTS);
        wl_put_str(&w, "FM1", WL_MAX_NAME);
        if (send(WL_HELLO_ACK, p, w.length) == -2) return; /* busy: retry next poll */
        ack_pending = 0;
        event_dirty = 1; /* tell the new session whether the page is open */
    }
    if (need_layout_pending) {
        if (send(WL_NEED_LAYOUT, NULL, 0) == -2) return;
        need_layout_pending = 0;
    }
    if (event_dirty && view.active) {
        uint8_t p[10];
        p[0] = (uint8_t)visible;
        p[1] = (uint8_t)event_buttons; p[2] = (uint8_t)(event_buttons >> 8);
        for (unsigned i = 0; i < 7; ++i) {
            int d = event_encoders[i];
            p[3 + i] = (uint8_t)(int8_t)(d > 127 ? 127 : d < -127 ? -127 : d);
        }
        if (send(WL_EVENT, p, sizeof p) == -2) return;
        for (unsigned i = 0; i < 7; ++i) {
            int sent = (int8_t)p[3 + i];
            event_encoders[i] -= sent; /* anything beyond +/-127 goes next time */
        }
        event_dirty = 0;
        for (unsigned i = 0; i < 7; ++i) event_dirty |= event_encoders[i] != 0;
    }
}

void groove_link_poll(void)
{
    uint32_t now = fm1_millis();
    while (inbox_read != inbox_write) {
        unsigned at = inbox_read % INBOX;
        barrier();
        receive(inbox[at].bytes, inbox[at].length, inbox[at].source, now);
        barrier();
        inbox_read = inbox_read + 1;
    }
    if (view.active && now - last_frame >= WL_TIMEOUT_MS) view.active = 0;
    flush();
}

void groove_link_view_copy(struct groove_link_view *out)
{
    if (out) *out = view;
}

unsigned groove_link_take_show(void)
{
    unsigned r = show_request;
    show_request = 0;
    return r;
}

void groove_link_set_visible(unsigned on)
{
    on = on != 0;
    if (on == visible) return;
    visible = on;
    if (!on) event_buttons = 0;
    event_dirty = 1;
    flush();
}

void groove_link_controls(uint32_t buttons, const int16_t encoders[7])
{
    if ((uint16_t)buttons != event_buttons) { event_buttons = (uint16_t)buttons; event_dirty = 1; }
    for (unsigned i = 0; i < 7; ++i)
        if (encoders[i]) { event_encoders[i] += encoders[i]; event_dirty = 1; }
    flush();
}
