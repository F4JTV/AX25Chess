/*
 * dw_msvc_compat.h - what the Dire Wolf sources expect from a Windows
 * compiler and the Microsoft C runtime does not give them.
 *
 * Included by dw_embed_shim.h when the compiler has the MSVC front end.
 * The Dire Wolf sources need a compiler that accepts GCC extensions
 * (__attribute__((hot)), statement expressions), which cl.exe does not;
 * clang-cl, shipped with Visual Studio, does.  This header supplies the
 * rest:
 *
 *   - __WIN32__, the macro MinGW defines and the Dire Wolf sources test
 *     to select their Windows code paths;
 *   - the BSD string comparison names, as the CRT's _stricmp/_strnicmp;
 *   - silence for the CRT's "unsafe function" and old-name deprecations,
 *     which the Dire Wolf sources trigger by the hundred.
 *
 * This file is part of the AX25Chess embedded Direwolf core.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DW_MSVC_COMPAT_H
#define DW_MSVC_COMPAT_H

#ifndef __WIN32__
#define __WIN32__ 1
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE 1
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#endif

/* Microsoft's math.h keeps M_PI and its relatives behind this macro, which
 * has to be defined before the header is first read; this file is
 * force-included before anything else, so this is early enough.  The
 * demodulators, the DSP filters and the tone generator all use M_PI. */
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 1
#endif
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>

/* POSIX names the Microsoft CRT does not have; MinGW's headers do, which is
 * why the MinGW cross-build never asked for them. */
#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif
#ifndef F_OK
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif

#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif

/* The re-entrant time conversions, which the Microsoft runtime spells the
 * other way round (localtime_s(result, time)). */

static __inline struct tm *dw_msvc_localtime_r (const time_t *t, struct tm *result)
{
	return (localtime_s (result, t) == 0) ? result : NULL;
}

static __inline struct tm *dw_msvc_gmtime_r (const time_t *t, struct tm *result)
{
	return (gmtime_s (result, t) == 0) ? result : NULL;
}

#ifndef localtime_r
#define localtime_r dw_msvc_localtime_r
#endif
#ifndef gmtime_r
#define gmtime_r dw_msvc_gmtime_r
#endif

#endif /* DW_MSVC_COMPAT_H */
