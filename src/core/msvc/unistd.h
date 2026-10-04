/*
 * unistd.h - stand-in for the POSIX header under the Microsoft C runtime.
 *
 * The Dire Wolf sources include <unistd.h> unconditionally and use a few
 * POSIX names that the Windows SDK does not provide.  With MinGW the header
 * exists; with clang-cl and the Visual Studio toolchain it does not, and
 * this directory is put on the include path instead (cmake/DirewolfCore.cmake).
 * Everything the Windows code paths of Dire Wolf actually call is in io.h,
 * process.h and direct.h; the rest is covered by dw_msvc_compat.h.
 *
 * This file is part of the AX25Chess embedded Direwolf core.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DW_MSVC_UNISTD_H
#define DW_MSVC_UNISTD_H

#include <io.h>
#include <process.h>
#include <direct.h>
#include <stdlib.h>
#include <BaseTsd.h>

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
#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED 1
#endif

#endif /* DW_MSVC_UNISTD_H */
