/*
 * dw_embed_shim.h - force-included into every Direwolf translation unit of
 * the embedded core (see cmake/DirewolfCore.cmake).
 *
 * Dire Wolf is written as a stand-alone program and calls exit() on fatal
 * conditions: a configuration error, an audio device that cannot be opened,
 * an input stream that fails, a PTT device that is missing.  That is the
 * right thing for a program and the wrong thing for a library living inside
 * a GUI process.
 *
 * This header includes <stdlib.h> first, so the real declaration of exit()
 * is in place, then rewrites the *token* exit in the Direwolf sources so that
 * every call lands in dw_embed_exit() (dw_embed.c).  That function reports
 * the fault to the host application and unwinds only the calling thread:
 *
 *   - on the engine thread (initialisation, dispatch loop) it longjmps back
 *     into dw_embed_start()/dw_embed_run(), which return an error;
 *   - on a thread that Direwolf created itself (audio input, transmit) it
 *     ends that thread.
 *
 * Nothing in the Direwolf sources uses "exit" as an identifier other than the
 * library call, so the rewrite is safe.  Our own files in src/core never call
 * exit() and are compiled with the same option for uniformity.
 *
 * This file is part of the AX25Chess embedded Direwolf core.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DW_EMBED_SHIM_H
#define DW_EMBED_SHIM_H

#if defined(_MSC_VER)
/* The Visual Studio toolchain (clang-cl): what the sources expect from a
 * Windows compiler, and the CRT headers that declare exit() with dllimport,
 * consumed here so the token rewrite below never touches them. */
#include "msvc/dw_msvc_compat.h"
#include <stdlib.h>
#include <process.h>
#elif defined(_WIN32)
/* MinGW: no CRT header here.  Every MinGW CRT header sets _WIN32_WINNT, and
 * direwolf.h refuses to be included after that.  MinGW's own declarations of
 * exit() carry no dllimport, so the rewrite turns them into compatible
 * redeclarations of dw_embed_exit(). */
#else
#include <stdlib.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
__declspec(noreturn) void dw_embed_exit (int status);
#elif defined(__GNUC__) || defined(__clang__)
void dw_embed_exit (int status) __attribute__((noreturn));
#else
void dw_embed_exit (int status);
#endif

#ifdef __cplusplus
}
#endif

#define exit(status) dw_embed_exit(status)

#endif /* DW_EMBED_SHIM_H */
