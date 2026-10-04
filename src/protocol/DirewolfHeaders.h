/*
 * DirewolfHeaders.h - the Direwolf headers the protocol layer uses, made
 * safe to include from C++.
 *
 * Two adjustments are needed:
 *
 *   - direwolf.h declares strcasestr() with a C signature that glibc's C++
 *     <string.h> rejects as an ambiguating overload.  The name is rewritten
 *     to an unused one for the duration of the include; nothing here calls
 *     strcasestr.
 *   - ax25_pad.h is built with AX25MEMDEBUG, so the packet constructors and
 *     destructor are macros that pass __FILE__ to a char* parameter, which
 *     C++ warns about.  Inline wrappers below call the underlying functions
 *     with an explicit cast.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#ifdef _WIN32
// direwolf.h insists on choosing the Windows API level itself and refuses
// to be included after _WIN32_WINNT is set, which Qt's build interface does
// on the command line; and windows.h's min/max macros would break std::min.
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#ifdef WINVER
#undef WINVER
#endif
#ifndef __WIN32__
#define __WIN32__ 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#endif

#define strcasestr dw_strcasestr_decl_unused
extern "C" {
#include "direwolf.h"
#include "ax25_pad.h"
#include "ax25_pad2.h"
#include "decode_aprs.h"
#include "latlong.h"
#include "fcs_calc.h"
#include "dw_textcolor.h"
}
#undef strcasestr

namespace dw {

inline char *here() { return const_cast<char *>("ax25chess-protocol"); }

inline packet_t fromText(char *monitor, int strict)
{
    return ax25_from_text_debug(monitor, strict, here(), 0);
}

inline packet_t fromFrame(unsigned char *data, int len)
{
    alevel_t alevel{};
    return ax25_from_frame_debug(data, len, alevel, here(), 0);
}

inline packet_t uiFrame(char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int numAddr, int pid,
                        unsigned char *info, int infoLen)
{
    return ax25_u_frame_debug(addrs, numAddr, cr_cmd, frame_type_U_UI, 0, pid, info, infoLen, here(), 0);
}

inline void del(packet_t pp)
{
    ax25_delete_debug(pp, here(), 0);
}

} // namespace dw
