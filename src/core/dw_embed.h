/*
 * dw_embed.h - C interface of the embedded Dire Wolf modem core.
 *
 * This is the only header a host application needs.  It replaces the three
 * things direwolf.c used to provide for the stand-alone program:
 *
 *   main()                  -> dw_embed_start() / dw_embed_run() / dw_embed_stop()
 *   app_process_rec_packet  -> the frame_received callback
 *   stdout                  -> the log callback (see dw_textcolor.c)
 *
 * Threading model
 * ---------------
 * dw_embed_start() and dw_embed_run() must be called from the same thread,
 * the "engine thread".  dw_embed_run() blocks until dw_embed_stop() is called
 * from any other thread.  Direwolf creates its own threads underneath (one
 * audio input thread per sound device, one transmit thread per radio
 * channel); they are stopped and joined by dw_embed_run() before it returns.
 *
 * Callbacks are invoked from the engine thread, except log(), which can come
 * from any Direwolf thread, and fault(), which is raised on the thread that
 * hit the fatal condition.  A Qt host must therefore treat every callback as
 * a cross-thread event and marshal it to the GUI thread itself.
 *
 * Re-entrancy
 * -----------
 * Direwolf keeps its state in file-scope statics, so at most one instance can
 * exist per process.  The core can be stopped and started again (that is what
 * patches/direwolf/0001-embedded-host-shutdown.patch is for), which is how the
 * host changes sound card or modem settings at run time.
 *
 * This file is part of the AX25Chess embedded Direwolf core.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DW_EMBED_H
#define DW_EMBED_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors Direwolf's enum dw_color_e, which is really a message category. */
typedef enum dw_embed_log_level_e {
	DW_EMBED_LOG_INFO = 0,
	DW_EMBED_LOG_ERROR,
	DW_EMBED_LOG_REC,	/* received frame monitor line */
	DW_EMBED_LOG_DECODED,	/* APRS decode explanation */
	DW_EMBED_LOG_XMIT,	/* transmitted frame monitor line */
	DW_EMBED_LOG_DEBUG
} dw_embed_log_level_t;

typedef enum dw_embed_fec_e {
	DW_EMBED_FEC_NONE = 0,
	DW_EMBED_FEC_FX25 = 1,
	DW_EMBED_FEC_IL2P = 2
} dw_embed_fec_t;

/* One received frame.  Pointers are valid only for the duration of the
 * callback; copy what you keep. */
typedef struct dw_embed_frame_s {
	int chan;			/* radio channel */
	int subchan;			/* demodulator that won, -1 for DTMF */
	int slice;			/* slicer that won */
	const unsigned char *data;	/* raw AX.25 frame, without FCS */
	int len;			/* number of octets in data */
	int alevel_rec;			/* audio level, percent of full scale */
	int alevel_mark;
	int alevel_space;
	dw_embed_fec_t fec;		/* how the frame was recovered */
	int retries;			/* bits fixed (AX.25) or bytes fixed (FX.25) */
	const char *spectrum;		/* "spectrum" string for multi-decoder channels */
} dw_embed_frame_t;

typedef struct dw_embed_callbacks_s {
	void *user;

	/* One complete line of Direwolf console output.  Any thread. */
	void (*log)(void *user, dw_embed_log_level_t level, const char *line);

	/* A frame passed the FCS (or FEC) check.  Engine thread. */
	void (*frame_received)(void *user, const dw_embed_frame_t *frame);

	/* Data carrier detect on a channel changed.  Engine thread.
	 * This is the modem's own carrier sense, which also fires on noise
	 * that never becomes a valid frame. */
	void (*dcd_changed)(void *user, int chan, int active);

	/* Our own transmitter was keyed or released.  Engine thread. */
	void (*ptt_changed)(void *user, int chan, int active);

	/* Direwolf hit a condition it would have exited on.  The thread that
	 * hit it.  After this the core is unusable until stopped and started
	 * again. */
	void (*fault)(void *user, int status, const char *reason);
} dw_embed_callbacks_t;

/* Lifecycle ----------------------------------------------------------- */

/* Parse the Direwolf configuration file, open the audio devices, start the
 * demodulators, the transmit threads and the audio input threads.
 * Returns 0 on success.  On failure returns -1, leaves a message in errbuf
 * and makes sure nothing is left running.  Engine thread. */
int dw_embed_start (const char *config_file,
		    const dw_embed_callbacks_t *callbacks,
		    char *errbuf, size_t errlen);

/* Dispatch received frames and link-layer events until dw_embed_stop() is
 * called, then shut everything down.  Returns 0 on a requested stop, -1 when
 * it returned because of a fault.  Engine thread. */
int dw_embed_run (void);

/* Ask dw_embed_run() to return.  Any thread.  Safe to call when not running. */
void dw_embed_stop (void);

/* True between a successful dw_embed_start() and the end of dw_embed_run(). */
int dw_embed_is_running (void);

/* Transmit --------------------------------------------------------------- */

/* Queue a raw AX.25 frame (no FCS) for transmission on a channel.  The frame
 * goes through Direwolf's own p-persistence channel access.  Returns 0, or
 * -1 if the channel is not a radio channel or the frame is malformed. */
int dw_embed_transmit (int chan, const unsigned char *frame, int len,
		       int high_priority);

/* Bytes still waiting in the transmit queue of a channel.  This is what the
 * KISS "TXBUF:" query used to report, now a direct call. */
int dw_embed_tx_queue_bytes (int chan);

/* Frames still waiting in the transmit queue of a channel. */
int dw_embed_tx_queue_frames (int chan);

/* Drop everything waiting in the transmit queue of a channel.  Returns the
 * number of frames discarded.  A frame already being sent is not affected. */
int dw_embed_tx_queue_clear (int chan);

/* Channel access parameters, same units as the KISS commands 1-5 and the
 * TXDELAY / PERSIST / SLOTTIME / TXTAIL / FULLDUP configuration keywords.
 * Pass -1 to leave a value unchanged. */
void dw_embed_set_channel_params (int chan, int txdelay, int persist,
				  int slottime, int txtail, int fulldup);

/* Channel state ---------------------------------------------------------- */

/* Data carrier detect right now. */
int dw_embed_dcd (int chan);

/* True when the channel exists and is served by the internal modem. */
int dw_embed_channel_is_radio (int chan);

/* Callsign configured with MYCALL for the channel, "" if none. */
const char *dw_embed_channel_mycall (int chan);

/* Modem description for the channel, e.g. "1200 baud AFSK 1200/2200". */
void dw_embed_channel_describe (int chan, char *buf, size_t buflen);

/* Maximum channel number the core can address (exclusive). */
int dw_embed_max_channels (void);

/* Version of the Direwolf sources compiled in, e.g. "1.8". */
const char *dw_embed_direwolf_version (void);

/* PTT backend (Android) --------------------------------------------------- */

/* On platforms where the program cannot reach a serial port or a HID device
 * itself (Android), the host keys the transmitter: ptt_android.c forwards
 * every PTT request to this backend, installed before dw_embed_start().
 * method: 0 serial (line 1 = RTS, 2 = DTR), 1 CM108 GPIO (gpio 1..8). */
typedef struct dw_embed_ptt_backend_s {
	void *user;
	int  (*open)(void *user, int chan, int method, const char *device, int gpio, int line, int line2);
	void (*set)(void *user, int chan, int on);
	void (*close)(void *user);
} dw_embed_ptt_backend_t;

void dw_embed_set_ptt_backend (const dw_embed_ptt_backend_t *backend);
const dw_embed_ptt_backend_t *dw_embed_ptt_backend (void);

/* Free-standing helpers ---------------------------------------------------- */

/* Load the symbol and device identification tables (symbols-new.txt,
 * tocalls.yaml, searched in the current directory, data/ and the usual
 * install locations).  Called by dw_embed_start(); call it yourself before
 * using Direwolf's decode_aprs() without the modem.  Safe to call twice. */
void dw_embed_init_tables (void);

/* Format the address part of a raw AX.25 frame the way Direwolf prints it
 * ("SRC>DST,DIGI*:").  Returns 0 or -1 if the frame does not parse. */
int dw_embed_format_addrs (const unsigned char *frame, int len,
			   char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* DW_EMBED_H */
