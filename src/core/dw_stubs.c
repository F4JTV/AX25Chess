/*
 * dw_stubs.c - no-op replacements for the Direwolf modules that the embedded
 * core leaves out (see cmake/DirewolfCore.cmake for the list and the reasons).
 *
 * The core still references a few of their functions:
 *
 *   server.c   AGW server callbacks from the connected-mode link layer
 *              (ax25_link.c) and from xmit.c's monitoring of our own frames
 *   igate.c    tq.c forwards frames of an IGate channel there
 *   nettnc.c   tq.c forwards frames of a network TNC channel there
 *   aprs_tt.c  recv.c reports DTMF buttons to the APRStt gateway
 *
 * None of those features exist in the embedded core, so the calls become
 * no-ops.  The prototypes are copied from the corresponding Direwolf headers
 * so that a signature change upstream is caught by the compiler.
 *
 * This file is part of the AX25Chess embedded Direwolf core.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#if defined(_MSC_VER)
#include "msvc/dw_msvc_compat.h"
#endif

#include "direwolf.h"
#include "ax25_pad.h"
#include "server.h"
#include "igate.h"
#include "nettnc.h"
#include "aprs_tt.h"

/* server.h --------------------------------------------------------------- */

void server_send_monitored (int chan, packet_t pp, int own_xmit)
{
	(void)chan; (void)pp; (void)own_xmit;
}

void server_link_established (int chan, int client, char *remote_call, char *own_call, int incoming)
{
	(void)chan; (void)client; (void)remote_call; (void)own_call; (void)incoming;
}

void server_link_terminated (int chan, int client, char *remote_call, char *own_call, int timeout)
{
	(void)chan; (void)client; (void)remote_call; (void)own_call; (void)timeout;
}

void server_rec_conn_data (int chan, int client, char *remote_call, char *own_call, int pid, char *data_ptr, int data_len)
{
	(void)chan; (void)client; (void)remote_call; (void)own_call; (void)pid; (void)data_ptr; (void)data_len;
}

void server_outstanding_frames_reply (int chan, int client, char *own_call, char *remote_call, int count)
{
	(void)chan; (void)client; (void)own_call; (void)remote_call; (void)count;
}

/* igate.h ---------------------------------------------------------------- */

void igate_send_rec_packet (int chan, packet_t recv_pp)
{
	/* tq_append() only gets here for a channel declared as an IGate
	 * channel, which the embedded core does not support; the packet is
	 * owned by the caller and freed there. */
	(void)chan; (void)recv_pp;
}

/* nettnc.h --------------------------------------------------------------- */

void nettnc_send_packet (int chan, packet_t pp)
{
	(void)chan; (void)pp;
}

/* aprs_tt.h -------------------------------------------------------------- */

void aprs_tt_button (int chan, char button)
{
	(void)chan; (void)button;
}

/* end dw_stubs.c */
