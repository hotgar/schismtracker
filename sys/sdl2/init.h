/*
 * Schism Tracker - a cross-platform Impulse Tracker clone
 * copyright (c) 2003-2005 Storlek <storlek@rigelseven.com>
 * copyright (c) 2005-2008 Mrs. Brisby <mrs.brisby@nimh.org>
 * copyright (c) 2009 Storlek & Mrs. Brisby
 * copyright (c) 2010-2012 Storlek
 * URL: http://schismtracker.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef SCHISM_SYS_SDL2_INIT_H_
#define SCHISM_SYS_SDL2_INIT_H_

// We have to include this **before** headers.h because
// otherwise tgmath.h doesn't work quite right combined
// with intrin.h under Win32. What the hell.

#include "headers.h"

/* stupid win32 crap */
#define NO_OLDNAMES
#define _NO_OLDNAMES

#ifdef SCHISM_OS2
// Work around weird compiler bug?
//
// ...erm, its because SDL expects a 486 or better.
// We probably should too.
# undef __386__
# include <SDL_endian.h>
# define __386__
#endif
#include <SDL.h>

int sdl2_init(void);
void sdl2_quit(void);

/* simple wrapper around SDL2_VERSION_ATLEAST, to remove the need for each backend
 * to look up SDL_GetVersion itself */
int sdl2_ver_atleast(int major, int minor, int patch);

#ifdef SDL2_DYNAMIC_LOAD

// must be called AFTER sdl2_init()
int sdl2_load_sym(const char *fn, void *addr);

#define SCHISM_SDL2_SYM(x) \
	if (!sdl2_load_sym("SDL_" #x, &sdl2_##x)) return -1

#else

#define SCHISM_SDL2_SYM(x) \
	sdl2_##x = SDL_##x

#endif

#endif /* SCHISM_SYS_SDL2_INIT_H_ */
