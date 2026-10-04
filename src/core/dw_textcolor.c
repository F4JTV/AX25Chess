/*
 * dw_textcolor.c - replaces src/textcolor.c of Dire Wolf.
 *
 * Every message Direwolf prints goes through text_color_set() to choose a
 * category and dw_printf() to emit text.  The original writes ANSI escape
 * sequences and text to stdout; this version assembles complete lines and
 * hands them to the host through a sink function, together with the category
 * in force when the line was started.
 *
 * Direwolf frequently prints a line in several calls ("[0.3] ", the
 * addresses, the information part, "\n"), from several threads at once, so
 * the partial line and the current category are kept per thread.
 *
 * The last line printed in the error category is remembered globally: when a
 * Direwolf module calls exit() it has almost always just printed the reason,
 * and dw_embed_exit() reports that line to the host as the fault reason.
 *
 * This file is part of the AX25Chess embedded Direwolf core.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#if defined(_MSC_VER)
#include "msvc/dw_msvc_compat.h"
#endif

#include "direwolf.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#if __WIN32__
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "textcolor.h"
#include "dw_textcolor.h"

#if defined(_MSC_VER)
#define DW_THREAD_LOCAL __declspec(thread)
#else
#define DW_THREAD_LOCAL _Thread_local
#endif

#define LINE_MAX_LEN 2000
#define CHUNK_MAX_LEN 4000

static dw_textcolor_sink_t g_sink = NULL;
static void *g_sink_user = NULL;

static char g_last_error[LINE_MAX_LEN];

#if __WIN32__
static CRITICAL_SECTION g_last_error_lock;
static int g_last_error_lock_ready = 0;
#else
static pthread_mutex_t g_last_error_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static DW_THREAD_LOCAL dw_color_t t_color = DW_COLOR_INFO;
static DW_THREAD_LOCAL char t_line[LINE_MAX_LEN];
static DW_THREAD_LOCAL int t_line_len = 0;

/* Last non-empty line this thread emitted, whatever its category.  When a
 * thread calls exit() it has usually just printed why. */
static DW_THREAD_LOCAL char t_last_line[LINE_MAX_LEN];

/* Optional per-thread diversion of the output, see dw_textcolor_capture_begin. */
static DW_THREAD_LOCAL dw_textcolor_sink_t t_capture_fn = NULL;
static DW_THREAD_LOCAL void *t_capture_user = NULL;


static void lock_last_error (void)
{
#if __WIN32__
	if ( ! g_last_error_lock_ready) {
	  /* text_color_init runs before any thread exists, see below. */
	  InitializeCriticalSection (&g_last_error_lock);
	  g_last_error_lock_ready = 1;
	}
	EnterCriticalSection (&g_last_error_lock);
#else
	pthread_mutex_lock (&g_last_error_lock);
#endif
}

static void unlock_last_error (void)
{
#if __WIN32__
	LeaveCriticalSection (&g_last_error_lock);
#else
	pthread_mutex_unlock (&g_last_error_lock);
#endif
}


/* Hand one finished line to the host. */

static void emit_line (void)
{
	t_line[t_line_len] = '\0';

	/* Strip a trailing carriage return; Direwolf uses "\r\n" in places. */
	while (t_line_len > 0 && t_line[t_line_len - 1] == '\r') {
	  t_line[--t_line_len] = '\0';
	}

	if (t_line_len > 0) {
	  memcpy (t_last_line, t_line, (size_t)t_line_len + 1);
	}

	if (t_color == DW_COLOR_ERROR && t_line_len > 0) {
	  lock_last_error ();
	  strncpy (g_last_error, t_line, sizeof(g_last_error) - 1);
	  g_last_error[sizeof(g_last_error) - 1] = '\0';
	  unlock_last_error ();
	}

	if (t_capture_fn != NULL) {
	  t_capture_fn (t_capture_user, (int)t_color, t_line);
	}
	else if (g_sink != NULL) {
	  g_sink (g_sink_user, (int)t_color, t_line);
	}
	else {
	  fputs (t_line, stderr);
	  fputc ('\n', stderr);
	}

	t_line_len = 0;
}


/* --- Direwolf's textcolor.h interface ------------------------------------ */

void text_color_init (int enable_color)
{
	(void)enable_color;
#if __WIN32__
	lock_last_error ();
	unlock_last_error ();
#endif
	lock_last_error ();
	g_last_error[0] = '\0';
	unlock_last_error ();
}

void text_color_set (dw_color_t c)
{
	/* A category change in the middle of a line starts a new line, which
	 * is how the coloured console output would have looked too. */
	if (c != t_color && t_line_len > 0) {
	  emit_line ();
	}
	t_color = c;
}

void text_color_term (void)
{
	if (t_line_len > 0) {
	  emit_line ();
	}
}

int dw_printf (const char *fmt, ...)
{
	char chunk[CHUNK_MAX_LEN];
	va_list args;
	int len;
	int i;

	va_start (args, fmt);
	len = vsnprintf (chunk, sizeof(chunk), fmt, args);
	va_end (args);

	if (len < 0) {
	  return (len);
	}
	if (len >= (int)sizeof(chunk)) {
	  len = (int)sizeof(chunk) - 1;
	}

	for (i = 0; i < len; i++) {
	  char ch = chunk[i];

	  if (ch == '\n') {
	    emit_line ();
	    continue;
	  }
	  if (t_line_len >= LINE_MAX_LEN - 1) {
	    emit_line ();
	  }
	  t_line[t_line_len++] = ch;
	}

	return (len);
}


/* --- Host side ------------------------------------------------------------- */

void dw_textcolor_set_sink (dw_textcolor_sink_t sink, void *user)
{
	g_sink = sink;
	g_sink_user = user;
}

void dw_textcolor_flush (void)
{
	if (t_line_len > 0) {
	  emit_line ();
	}
}

void dw_textcolor_capture_begin (dw_textcolor_sink_t fn, void *user)
{
	if (t_line_len > 0) {
	  emit_line ();
	}
	t_capture_fn = fn;
	t_capture_user = user;
}

void dw_textcolor_capture_end (void)
{
	if (t_line_len > 0) {
	  emit_line ();
	}
	t_capture_fn = NULL;
	t_capture_user = NULL;
}

void dw_textcolor_last_line (char *buf, size_t buflen)
{
	if (buf == NULL || buflen == 0) {
	  return;
	}
	strncpy (buf, t_last_line, buflen - 1);
	buf[buflen - 1] = '\0';
}

void dw_textcolor_last_error (char *buf, size_t buflen)
{
	if (buf == NULL || buflen == 0) {
	  return;
	}
	lock_last_error ();
	strncpy (buf, g_last_error, buflen - 1);
	buf[buflen - 1] = '\0';
	unlock_last_error ();
}

/* end dw_textcolor.c */
