/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Workshop Link, device side: a Workshop Computer card (USB host) shows
 * widgets on the FM1 screen and reads the FM1 encoders/buttons back.
 * Protocol: WorkshopLink.h (shared verbatim with Workshop_Computer). */
#ifndef GROOVE_WORKSHOP_LINK_H
#define GROOVE_WORKSHOP_LINK_H
#include "fm1.h"
#include "WorkshopLink.h"

struct groove_link_widget {
    uint8_t type, x, y, w, h, color;
    uint8_t scope;                 /* scope slot for WL_W_SCOPE, else 0xff */
    int16_t min, max, value;
    char label[WL_MAX_LABEL + 1];
    char text[WL_MAX_TEXT + 1];
};
struct groove_link_view {
    uint8_t active;                /* a host session is alive */
    uint8_t count;                 /* widgets defined: ids 0..count-1 */
    uint32_t revision;             /* changes with every layout change */
    char title[WL_MAX_TEXT + 1];
    char peer[WL_MAX_NAME + 1];    /* the card's name */
    struct groove_link_widget widget[WL_MAX_WIDGETS];
    uint8_t scope_points[WL_MAX_SCOPES];
    int8_t scope[WL_MAX_SCOPES][WL_MAX_SCOPE_POINTS];
};

/* Routing task (fm1_app_sysex): returns 1 when the frame belongs to the link
 * (it is queued or dropped), 0 when it is someone else's SysEx. */
int groove_link_sysex(const uint8_t *message, size_t bytes);
/* Control task. */
void groove_link_poll(void);
void groove_link_view_copy(struct groove_link_view *out);
/* 0 nothing, 1 release the link page, 2 open it. Clears the request. */
unsigned groove_link_take_show(void);
/* Report whether the link page is on screen, and its panel input. Buttons
 * exclude HOME, which always leaves the page. */
void groove_link_set_visible(unsigned visible);
void groove_link_controls(uint32_t buttons, const int16_t encoders[7]);
#endif
