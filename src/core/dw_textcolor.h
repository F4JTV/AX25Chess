/*
 * dw_textcolor.h - host side of the textcolor.c replacement.
 *
 * This file is part of the AX25Chess embedded Direwolf core.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DW_TEXTCOLOR_H
#define DW_TEXTCOLOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* level is Direwolf's enum dw_color_e cast to int, which is identical to
 * dw_embed_log_level_t in dw_embed.h.  Called from any thread. */
typedef void (*dw_textcolor_sink_t)(void *user, int level, const char *line);

void dw_textcolor_set_sink (dw_textcolor_sink_t sink, void *user);

/* Divert everything the calling thread prints to fn instead of the sink,
 * until dw_textcolor_capture_end().  Used to collect the messages that
 * decode_aprs() prints about a packet while decoding it on the host side. */
void dw_textcolor_capture_begin (dw_textcolor_sink_t fn, void *user);
void dw_textcolor_capture_end (void);

/* Emit a partial line held for the calling thread, if any. */
void dw_textcolor_flush (void);

/* Copy of the most recent line the calling thread printed, any category. */
void dw_textcolor_last_line (char *buf, size_t buflen);

/* Copy of the most recent line printed in the error category, any thread. */
void dw_textcolor_last_error (char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* DW_TEXTCOLOR_H */
