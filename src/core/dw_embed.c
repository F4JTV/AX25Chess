/*
 * dw_embed.c - embedding wrapper around the Dire Wolf modem core.
 *
 * This file takes the place of direwolf.c.  It performs the same
 * initialisation sequence as main() there, minus the parts that only make
 * sense for the stand-alone program (command line, KISS/AGW servers, its own
 * beacons, digipeater, IGate, DTMF gateway, log files, GPS), runs the same
 * dispatch loop as recv_process() with a stop flag added, and implements
 * app_process_rec_packet(), which recv.c calls for every decoded frame.
 *
 * See dw_embed.h for the threading model.
 *
 * This file is part of the AX25Chess embedded Direwolf core.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dw_embed_shim.h"
#undef exit

#include "direwolf.h"

#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>

#if __WIN32__
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include "audio.h"
#include "config.h"
#include "ax25_pad.h"
#include "ax25_link.h"
#include "digipeater.h"
#include "cdigipeater.h"
#include "aprs_tt.h"
#include "igate.h"
#include "dlq.h"
#include "tq.h"
#include "xmit.h"
#include "recv.h"
#include "multi_modem.h"
#include "hdlc_rec.h"
#include "hdlc_rec2.h"
#include "fx25.h"
#include "il2p.h"
#include "gen_tone.h"
#include "morse.h"
#include "dtmf.h"
#include "ptt.h"
#include "symbols.h"
#include "deviceid.h"
#include "dtime_now.h"
#include "textcolor.h"

#include "dw_embed.h"
#include "dw_textcolor.h"

#ifndef DW_DIREWOLF_VERSION
#define DW_DIREWOLF_VERSION "unknown"
#endif

#if defined(_MSC_VER)
#define DW_THREAD_LOCAL __declspec(thread)
#else
#define DW_THREAD_LOCAL _Thread_local
#endif

/* Same default as direwolf.c: percent of full scale for generated audio. */
static const int audio_amplitude = 100;

/* How long the dispatch loop sleeps between checks of the stop flag. */
static const double DISPATCH_TICK_SEC = 0.25;

/* How long to wait for Direwolf's own threads to notice a stop request. */
static const int THREAD_STOP_TIMEOUT_MS = 2000;


/* ---- State -------------------------------------------------------------- */

/* The stand-alone program keeps these on the stack of main(). */
static struct audio_s audio_config;
static struct digi_config_s digi_config;
static struct cdigi_config_s cdigi_config;
static struct tt_config_s tt_config;
static struct igate_config_s igate_config;
static struct misc_config_s misc_config;

static dw_embed_callbacks_t cb;

static volatile int g_running = 0;	/* between start and end of run */
static volatile int g_stop_requested = 0;
static volatile int g_faulted = 0;
static int g_audio_open = 0;

/* Where dw_embed_exit() unwinds to on the engine thread. */
static DW_THREAD_LOCAL jmp_buf *t_exit_target = NULL;


/* ---- Logging ------------------------------------------------------------ */

static void log_sink (void *user, int level, const char *line)
{
	(void)user;
	if (cb.log != NULL) {
	  cb.log (cb.user, (dw_embed_log_level_t)level, line);
	}
}

static void log_line (dw_embed_log_level_t level, const char *line)
{
	if (cb.log != NULL) {
	  cb.log (cb.user, level, line);
	}
}


/* ---- exit() replacement ----------------------------------------------- */

/* Every exit() in the Direwolf sources arrives here (dw_embed_shim.h).
 * The calling thread has almost always just printed the condition, so its
 * last line is reported as the reason; failing that, the last error line of
 * any thread. */

void dw_embed_exit (int status)
{
	char reason[500];

	dw_textcolor_flush ();
	dw_textcolor_last_line (reason, sizeof(reason));
	if (reason[0] == '\0') {
	  dw_textcolor_last_error (reason, sizeof(reason));
	}
	if (reason[0] == '\0') {
	  snprintf (reason, sizeof(reason), "Direwolf requested exit(%d)", status);
	}

	g_faulted = 1;

	if (cb.fault != NULL) {
	  cb.fault (cb.user, status, reason);
	}

	if (t_exit_target != NULL) {
	  /* Engine thread: back to dw_embed_start() or dw_embed_run(). */
	  longjmp (*t_exit_target, status == 0 ? 1 : status);
	}

	/* A thread Direwolf created (audio input, transmit, PTT input).  End
	 * it; the host will see the fault and stop the core.  An audio input
	 * thread tells recv.c it is gone so the shutdown does not wait for it. */
	recv_thread_exiting ();
#if __WIN32__
	_endthreadex ((unsigned)status);
#else
	pthread_exit (NULL);
#endif
	/* Not reached, but the shim declares us noreturn. */
	for (;;) { SLEEP_SEC(1); }
}


/* ---- Received frames ---------------------------------------------------- */

/* Called by recv.c (through the dispatch loop below) for every frame that
 * passed the FCS or FEC check.  The stand-alone version prints the frame,
 * feeds the KISS/AGW clients, the digipeater and the IGate; the embedded
 * version hands the raw frame to the host and nothing else.  The packet is
 * owned by the queue item and freed by dlq_delete() after we return. */

void app_process_rec_packet (int chan, int subchan, int slice, packet_t pp,
			     alevel_t alevel, fec_type_t fec_type,
			     retry_t retries, char *spectrum)
{
	unsigned char fbuf[AX25_MAX_PACKET_LEN];
	dw_embed_frame_t frame;
	int flen;

	assert (pp != NULL);

	flen = ax25_pack (pp, fbuf);
	if (flen <= 0) {
	  return;
	}

	memset (&frame, 0, sizeof(frame));
	frame.chan = chan;
	frame.subchan = subchan;
	frame.slice = slice;
	frame.data = fbuf;
	frame.len = flen;
	frame.alevel_rec = alevel.rec;
	frame.alevel_mark = alevel.mark;
	frame.alevel_space = alevel.space;
	frame.fec = (dw_embed_fec_t)fec_type;
	frame.retries = (int)retries;
	frame.spectrum = spectrum;

	if (cb.frame_received != NULL) {
	  cb.frame_received (cb.user, &frame);
	}
}


/* ---- Tables used by the decoders ---------------------------------------- */

/* symbols_init() and deviceid_init() load symbols-new.txt and tocalls.yaml,
 * neither is guarded against being called twice, and the APRS decoder can
 * be used before the modem is started (unit tests, offline decoding), so
 * both go through this once-only function. */

static int g_tables_ready = 0;

void dw_embed_init_tables (void)
{
	if (g_tables_ready) {
	  return;
	}
	g_tables_ready = 1;
	symbols_init ();
	deviceid_init ();
}


/* ---- Start -------------------------------------------------------------- */

static void stop_direwolf_threads (void);

int dw_embed_start (const char *config_file,
		    const dw_embed_callbacks_t *callbacks,
		    char *errbuf, size_t errlen)
{
	jmp_buf jb;
	char msg[600];

	if (errbuf != NULL && errlen > 0) {
	  errbuf[0] = '\0';
	}

	if (g_running) {
	  if (errbuf != NULL) snprintf (errbuf, errlen, "Modem core is already running");
	  return (-1);
	}

	if (callbacks != NULL) {
	  cb = *callbacks;
	}
	else {
	  memset (&cb, 0, sizeof(cb));
	}

	g_stop_requested = 0;
	g_faulted = 0;
	g_audio_open = 0;

	dw_textcolor_set_sink (log_sink, NULL);
	text_color_init (0);

	/* Anything below that would have exited the program lands here. */
	t_exit_target = &jb;
	if (setjmp (jb) != 0) {
	  t_exit_target = NULL;
	  dw_textcolor_last_error (msg, sizeof(msg));
	  if (errbuf != NULL) {
	    snprintf (errbuf, errlen, "%s", msg[0] ? msg : "Direwolf aborted during start-up");
	  }
	  stop_direwolf_threads ();
	  if (g_audio_open) {
	    audio_close ();
	    g_audio_open = 0;
	  }
	  return (-1);
	}

	memset (&audio_config, 0, sizeof(audio_config));
	memset (&digi_config, 0, sizeof(digi_config));
	memset (&cdigi_config, 0, sizeof(cdigi_config));
	memset (&tt_config, 0, sizeof(tt_config));
	memset (&igate_config, 0, sizeof(igate_config));
	memset (&misc_config, 0, sizeof(misc_config));

	snprintf (msg, sizeof(msg), "Dire Wolf %s core starting, configuration %s",
		  DW_DIREWOLF_VERSION, config_file != NULL ? config_file : "(default)");
	log_line (DW_EMBED_LOG_INFO, msg);

	/* Same order as main() in direwolf.c. */

	dw_embed_init_tables ();

	config_init ((char *)config_file, &audio_config, &digi_config, &cdigi_config,
		     &tt_config, &igate_config, &misc_config);

	if (audio_open (&audio_config) < 0) {
	  t_exit_target = NULL;
	  dw_textcolor_last_error (msg, sizeof(msg));
	  if (errbuf != NULL) {
	    snprintf (errbuf, errlen, "Could not open the audio device: %s",
		      msg[0] ? msg : "see the log");
	  }
	  return (-1);
	}
	g_audio_open = 1;

	multi_modem_init (&audio_config);
	fx25_init (0);
	il2p_init (0);

	dtmf_init (&audio_config, audio_amplitude);
	gen_tone_init (&audio_config, audio_amplitude, 0);
	morse_init (&audio_config, audio_amplitude);

	/* Starts one transmit thread per radio channel and initialises PTT. */
	xmit_init (&audio_config, 0);

	/* The connected-mode link layer.  We do not use it, but recv.c's
	 * dispatch loop and the DLQ expect it to be initialised. */
	ax25_link_init (&misc_config, 0);

	/* Starts one audio input thread per sound device. */
	recv_init (&audio_config);

	t_exit_target = NULL;
	g_running = 1;

	log_line (DW_EMBED_LOG_INFO, "Dire Wolf core started");
	return (0);
}


/* ---- Stop --------------------------------------------------------------- */

void dw_embed_stop (void)
{
	g_stop_requested = 1;
}

int dw_embed_is_running (void)
{
	return (g_running);
}

/* Ask the threads Direwolf created to finish and wait for them.
 * Requires patches/direwolf/0001-embedded-host-shutdown.patch. */

static void stop_direwolf_threads (void)
{
	int waited;

	/* Audio input threads notice within one audio period. */
	recv_term ();
	for (waited = 0; recv_threads_active () > 0 && waited < THREAD_STOP_TIMEOUT_MS; waited += 10) {
	  SLEEP_MS (10);
	}
	if (recv_threads_active () > 0) {
	  log_line (DW_EMBED_LOG_ERROR, "Audio input thread did not stop; leaving the device open");
	}

	/* Transmit threads wake up and return. */
	tq_term ();
	SLEEP_MS (50);

	/* Release PTT lines and close the PTT devices. */
	ptt_term ();
}


/* ---- Dispatch loop ------------------------------------------------------ */

/* Same as recv_process() in recv.c, with a bounded wait so the stop flag is
 * seen, and with channel activity reported to the host. */

int dw_embed_run (void)
{
	jmp_buf jb;
	struct dlq_item_s *pitem;
	int result = 0;

	if ( ! g_running) {
	  return (-1);
	}

	t_exit_target = &jb;
	if (setjmp (jb) != 0) {
	  /* A fault on the engine thread; fall through to the shutdown. */
	  result = -1;
	  goto shutdown;
	}

	while ( ! g_stop_requested && ! g_faulted) {

	  double now = dtime_now ();
	  double timeout = ax25_link_get_next_timer_expiry ();
	  int timed_out;

	  if (timeout == 0.0 || timeout > now + DISPATCH_TICK_SEC) {
	    timeout = now + DISPATCH_TICK_SEC;
	  }

	  timed_out = dlq_wait_while_empty (timeout);

	  if (timed_out) {
	    dl_timer_expiry ();
	    continue;
	  }

	  pitem = dlq_remove ();
	  if (pitem == NULL) {
	    continue;
	  }

	  switch (pitem->type) {

	    case DLQ_REC_FRAME:
	      app_process_rec_packet (pitem->chan, pitem->subchan, pitem->slice,
				      pitem->pp, pitem->alevel, pitem->fec_type,
				      pitem->retries, pitem->spectrum);
	      lm_data_indication (pitem);
	      break;

	    case DLQ_CONNECT_REQUEST:
	      dl_connect_request (pitem);
	      break;

	    case DLQ_DISCONNECT_REQUEST:
	      dl_disconnect_request (pitem);
	      break;

	    case DLQ_XMIT_DATA_REQUEST:
	      dl_data_request (pitem);
	      break;

	    case DLQ_REGISTER_CALLSIGN:
	      dl_register_callsign (pitem);
	      break;

	    case DLQ_UNREGISTER_CALLSIGN:
	      dl_unregister_callsign (pitem);
	      break;

	    case DLQ_OUTSTANDING_FRAMES_REQUEST:
	      dl_outstanding_frames_request (pitem);
	      break;

	    case DLQ_CHANNEL_BUSY:
	      /* Raised by ptt_set() for both our own PTT and the DCD of
	       * the demodulators.  This is the real carrier sense. */
	      if (pitem->activity == OCTYPE_DCD) {
	        if (cb.dcd_changed != NULL) {
	          cb.dcd_changed (cb.user, pitem->chan, pitem->status);
	        }
	      }
	      else if (pitem->activity == OCTYPE_PTT) {
	        if (cb.ptt_changed != NULL) {
	          cb.ptt_changed (cb.user, pitem->chan, pitem->status);
	        }
	      }
	      lm_channel_busy (pitem);
	      break;

	    case DLQ_SEIZE_CONFIRM:
	      lm_seize_confirm (pitem);
	      break;

	    case DLQ_CLIENT_CLEANUP:
	      dl_client_cleanup (pitem);
	      break;
	  }

	  dlq_delete (pitem);
	}

	if (g_faulted) {
	  result = -1;
	}

shutdown:
	t_exit_target = NULL;

	stop_direwolf_threads ();

	if (g_audio_open) {
	  audio_close ();
	  g_audio_open = 0;
	}

	/* Drain whatever the input threads queued after we left the loop. */
	while ((pitem = dlq_remove ()) != NULL) {
	  dlq_delete (pitem);
	}

	dw_textcolor_flush ();
	g_running = 0;

	log_line (DW_EMBED_LOG_INFO, result == 0 ? "Dire Wolf core stopped"
					       : "Dire Wolf core stopped after a fault");
	return (result);
}


/* ---- Transmit ----------------------------------------------------------- */

int dw_embed_channel_is_radio (int chan)
{
	if (chan < 0 || chan >= MAX_RADIO_CHANS) {
	  return (0);
	}
	return (audio_config.chan_medium[chan] == MEDIUM_RADIO);
}

int dw_embed_transmit (int chan, const unsigned char *frame, int len,
		       int high_priority)
{
	alevel_t alevel;
	packet_t pp;

	if ( ! g_running || ! dw_embed_channel_is_radio (chan)) {
	  return (-1);
	}
	if (frame == NULL || len < AX25_MIN_PACKET_LEN || len > AX25_MAX_PACKET_LEN) {
	  return (-1);
	}

	memset (&alevel, 0, sizeof(alevel));
	pp = ax25_from_frame ((unsigned char *)frame, len, alevel);
	if (pp == NULL) {
	  return (-1);
	}

	tq_append (chan, high_priority ? TQ_PRIO_0_HI : TQ_PRIO_1_LO, pp);
	return (0);
}

int dw_embed_tx_queue_bytes (int chan)
{
	if ( ! g_running || ! dw_embed_channel_is_radio (chan)) {
	  return (0);
	}
	return (tq_count (chan, -1, "", "", 1));
}

int dw_embed_tx_queue_frames (int chan)
{
	if ( ! g_running || ! dw_embed_channel_is_radio (chan)) {
	  return (0);
	}
	return (tq_count (chan, -1, "", "", 0));
}

int dw_embed_tx_queue_clear (int chan)
{
	packet_t pp;
	int n = 0;

	if ( ! g_running || ! dw_embed_channel_is_radio (chan)) {
	  return (0);
	}
	while ((pp = tq_remove (chan, TQ_PRIO_0_HI)) != NULL) {
	  ax25_delete (pp);
	  n++;
	}
	while ((pp = tq_remove (chan, TQ_PRIO_1_LO)) != NULL) {
	  ax25_delete (pp);
	  n++;
	}
	return (n);
}

void dw_embed_set_channel_params (int chan, int txdelay, int persist,
				  int slottime, int txtail, int fulldup)
{
	if ( ! g_running || ! dw_embed_channel_is_radio (chan)) {
	  return;
	}
	if (txdelay >= 0)  xmit_set_txdelay (chan, txdelay);
	if (persist >= 0)  xmit_set_persist (chan, persist);
	if (slottime >= 0) xmit_set_slottime (chan, slottime);
	if (txtail >= 0)   xmit_set_txtail (chan, txtail);
	if (fulldup >= 0)  xmit_set_fulldup (chan, fulldup ? 1 : 0);
}


/* ---- Channel state ------------------------------------------------------ */

int dw_embed_dcd (int chan)
{
	if ( ! g_running || ! dw_embed_channel_is_radio (chan)) {
	  return (0);
	}
	return (hdlc_rec_data_detect_any (chan));
}

const char *dw_embed_channel_mycall (int chan)
{
	if (chan < 0 || chan >= MAX_TOTAL_CHANS) {
	  return ("");
	}
	return (audio_config.mycall[chan]);
}

void dw_embed_channel_describe (int chan, char *buf, size_t buflen)
{
	if (buf == NULL || buflen == 0) {
	  return;
	}
	buf[0] = '\0';
	if ( ! dw_embed_channel_is_radio (chan)) {
	  snprintf (buf, buflen, "not a radio channel");
	  return;
	}
	switch (audio_config.achan[chan].modem_type) {
	  case MODEM_AFSK:
	    snprintf (buf, buflen, "%d baud AFSK %d/%d", audio_config.achan[chan].baud,
		      audio_config.achan[chan].mark_freq, audio_config.achan[chan].space_freq);
	    break;
	  case MODEM_QPSK:
	    snprintf (buf, buflen, "%d bps QPSK", audio_config.achan[chan].baud);
	    break;
	  case MODEM_8PSK:
	    snprintf (buf, buflen, "%d bps 8PSK", audio_config.achan[chan].baud);
	    break;
	  case MODEM_SCRAMBLE:
	    snprintf (buf, buflen, "%d baud scrambled (G3RUH)", audio_config.achan[chan].baud);
	    break;
	  case MODEM_BASEBAND:
	    snprintf (buf, buflen, "%d baud baseband", audio_config.achan[chan].baud);
	    break;
	  default:
	    snprintf (buf, buflen, "%d baud", audio_config.achan[chan].baud);
	    break;
	}
}

int dw_embed_max_channels (void)
{
	return (MAX_RADIO_CHANS);
}

const char *dw_embed_direwolf_version (void)
{
	return (DW_DIREWOLF_VERSION);
}


/* ---- PTT backend --------------------------------------------------------- */

static dw_embed_ptt_backend_t g_ptt_backend;
static int g_ptt_backend_set = 0;

void dw_embed_set_ptt_backend (const dw_embed_ptt_backend_t *backend)
{
	if (backend != NULL) {
	  g_ptt_backend = *backend;
	  g_ptt_backend_set = 1;
	}
	else {
	  memset (&g_ptt_backend, 0, sizeof(g_ptt_backend));
	  g_ptt_backend_set = 0;
	}
}

const dw_embed_ptt_backend_t *dw_embed_ptt_backend (void)
{
	return (g_ptt_backend_set ? &g_ptt_backend : NULL);
}


/* ---- Helpers ------------------------------------------------------------ */

int dw_embed_format_addrs (const unsigned char *frame, int len,
			   char *buf, size_t buflen)
{
	alevel_t alevel;
	packet_t pp;
	char stemp[AX25_MAX_ADDRS * AX25_MAX_ADDR_LEN + 20];

	if (buf == NULL || buflen == 0) {
	  return (-1);
	}
	buf[0] = '\0';

	memset (&alevel, 0, sizeof(alevel));
	pp = ax25_from_frame ((unsigned char *)frame, len, alevel);
	if (pp == NULL) {
	  return (-1);
	}
	ax25_format_addrs (pp, stemp);
	ax25_delete (pp);

	snprintf (buf, buflen, "%s", stemp);
	return (0);
}

/* end dw_embed.c */
