/*
 * ptt_android.c - replaces src/ptt.c of Dire Wolf on Android.
 *
 * An Android application has no /dev/ttyUSB0 and no /dev/hidraw0: USB
 * devices are reached through the platform's USB host API, from Java. So
 * this file keeps ptt.c's contract towards the rest of the core - the
 * DLQ notification that feeds carrier sense and the invert flags - and
 * hands the actual keying to the host through dw_embed_ptt_backend().
 *
 * Configuration lines keep their Dire Wolf syntax: "PTT CM108 3" keys the
 * GPIO of a USB sound card, "PTT /dev/ttyUSB0 RTS" the RTS line of a USB
 * serial adapter; the device name is ignored, the host picks the USB
 * device.  config.c is compiled with USE_CM108 so it accepts the CM108
 * keyword; the two cm108.c functions it references are provided below.
 *
 * This file is part of the AX25Chess embedded Direwolf core.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "direwolf.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "audio.h"
#include "ptt.h"
#include "dlq.h"
#include "textcolor.h"
#include "cm108.h"
#include "demod.h"		/* demod_mute_input, as ptt.c */
#include "dw_embed.h"

static struct audio_s *save_audio_config_p = NULL;
static int ptt_debug_level = 0;
static int opened[MAX_RADIO_CHANS];

void ptt_set_debug (int debug)
{
	ptt_debug_level = debug;
}

void ptt_init (struct audio_s *audio_config_p)
{
	int ch;
	const dw_embed_ptt_backend_t *backend = dw_embed_ptt_backend ();

	save_audio_config_p = audio_config_p;
	memset (opened, 0, sizeof(opened));

	for (ch = 0; ch < MAX_RADIO_CHANS; ch++) {
	  struct audio_s *p = audio_config_p;
	  int method, line, line2;

	  if (p->chan_medium[ch] != MEDIUM_RADIO) continue;

	  switch (p->achan[ch].octrl[OCTYPE_PTT].ptt_method) {
	    case PTT_METHOD_NONE:
	      continue;
	    case PTT_METHOD_SERIAL:
	      method = 0;
	      line = (p->achan[ch].octrl[OCTYPE_PTT].ptt_line == PTT_LINE_DTR) ? 2 : 1;
	      line2 = (p->achan[ch].octrl[OCTYPE_PTT].ptt_line2 == PTT_LINE_DTR) ? 2
	            : (p->achan[ch].octrl[OCTYPE_PTT].ptt_line2 == PTT_LINE_RTS) ? 1 : 0;
	      break;
	    case PTT_METHOD_CM108:
	      method = 1;
	      line = 0;
	      line2 = 0;
	      break;
	    default:
	      text_color_set (DW_COLOR_ERROR);
	      dw_printf ("Channel %d: this PTT method is not available on Android; use CM108 or a serial line (RTS/DTR).\n", ch);
	      continue;
	  }

	  if (backend == NULL || backend->open == NULL) {
	    text_color_set (DW_COLOR_ERROR);
	    dw_printf ("Channel %d: PTT configured but no USB PTT backend is installed.\n", ch);
	    continue;
	  }
	  if (backend->open (backend->user, ch, method, p->achan[ch].octrl[OCTYPE_PTT].ptt_device,
	                     p->achan[ch].octrl[OCTYPE_PTT].out_gpio_num, line, line2) == 0) {
	    opened[ch] = 1;
	    text_color_set (DW_COLOR_INFO);
	    dw_printf ("Channel %d: PTT through the USB %s.\n", ch, method == 1 ? "sound card GPIO" : "serial adapter");
	  }
	  else {
	    text_color_set (DW_COLOR_ERROR);
	    dw_printf ("Channel %d: the USB PTT device could not be opened; transmissions will rely on VOX.\n", ch);
	  }
	}
}

void ptt_set (int ot, int chan, int ptt_signal)
{
	int ptt = ptt_signal;
	const dw_embed_ptt_backend_t *backend = dw_embed_ptt_backend ();

	assert (ot >= 0 && ot < NUM_OCTYPES);
	assert (chan >= 0 && chan < MAX_RADIO_CHANS);

	if (ptt_debug_level >= 1) {
	  text_color_set (DW_COLOR_DEBUG);
	  dw_printf ("%s %d = %d\n", ot == OCTYPE_PTT ? "PTT" : ot == OCTYPE_DCD ? "DCD" : "CON", chan, ptt_signal);
	}
	if (save_audio_config_p == NULL || save_audio_config_p->chan_medium[chan] != MEDIUM_RADIO) {
	  return;
	}

	/* Same as ptt.c: in half duplex the receiver is muted while we
	 * transmit.  The phone's input hears our own signal through the
	 * interface; decoded, it holds the channel busy and shows our frames
	 * as received. */
	if (ot == OCTYPE_PTT && ! save_audio_config_p->achan[chan].fulldup) {
	  demod_mute_input (chan, ptt_signal);
	}

	/* Same as ptt.c: the link layer and the host learn about our own
	 * transmissions and about the carrier detect through the DLQ. */
	dlq_channel_busy (chan, ot, ptt_signal);

	if (save_audio_config_p->achan[chan].octrl[ot].ptt_invert) {
	  ptt = ! ptt;
	}

	if (ot == OCTYPE_PTT && opened[chan] && backend != NULL && backend->set != NULL) {
	  backend->set (backend->user, chan, ptt);
	}
}

void ptt_term (void)
{
	const dw_embed_ptt_backend_t *backend = dw_embed_ptt_backend ();
	int ch;

	for (ch = 0; ch < MAX_RADIO_CHANS; ch++) {
	  if (opened[ch] && backend != NULL && backend->set != NULL) {
	    backend->set (backend->user, ch, 0);
	  }
	  opened[ch] = 0;
	}
	if (backend != NULL && backend->close != NULL) {
	  backend->close (backend->user);
	}
}

int get_input (int it, int chan)
{
	(void)it; (void)chan;
	return (-1);		/* no input lines on Android */
}

/* cm108.h, for config.c ------------------------------------------------- */

void cm108_find_ptt (char *output_audio_device, char *ptt_device, int ptt_device_size)
{
	(void)output_audio_device;
	/* Any non-empty name satisfies config.c; the host picks the device. */
	strlcpy (ptt_device, "usb", ptt_device_size);
}

int cm108_set_gpio_pin (char *name, int num, int state)
{
	(void)name; (void)num; (void)state;
	return (-1);
}

/* end ptt_android.c */
