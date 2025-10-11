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
#ifndef SCHISM_CHARSET_H_
#define SCHISM_CHARSET_H_

#include "headers.h"

typedef enum {
	/* Unicode */
	CHARSET_UCS4LE = 0,
	CHARSET_UCS4BE,
	CHARSET_UTF16LE,
	CHARSET_UTF16BE,
	CHARSET_UCS2LE,
	CHARSET_UCS2BE,
	CHARSET_UTF8,
#ifdef WORDS_BIGENDIAN
# define CHARSET_UCS4  CHARSET_UCS4BE
# define CHARSET_UTF16 CHARSET_UTF16BE
# define CHARSET_UCS2  CHARSET_UCS2BE
#else
# define CHARSET_UCS4  CHARSET_UCS4LE
# define CHARSET_UTF16 CHARSET_UTF16LE
# define CHARSET_UCS2  CHARSET_UCS2LE
#endif

	/* Impulse Tracker built in font */
	CHARSET_ITF,

	/* European languages */
	CHARSET_CP437,
	CHARSET_WINDOWS1252, /* thanks modplug! */

	/* NOTE: CHARSET_CHAR is actually just a synonym for CHARSET_UTF8 now.
	 *
	 * Originally, it was supposed to sort-of represent the system encoding,
	 * which on Windows would be ANSI, MacOS the system script, Unix-like
	 * the actual encoding of `char`... but that plan fell out after I dumped
	 * SDL out of the main source tree. So now it just serves as a stupid ugly
	 * wart in the source that doesn't mean what it ought to mean.
	 *
	 * Really every place that uses CHARSET_CHAR actually does mean UTF-8
	 * because we handle file paths internally as UTF-8 on all platforms.
	 * (or at least expect them to be in UTF-8, maybe some weird old linux
	 * systems use latin-1 or whatever) */
	CHARSET_CHAR,
	CHARSET_WCHAR_T,

	/* START SYSTEM-SPECIFIC HACKS */
#if defined(SCHISM_WIN32) || defined(SCHISM_XBOX)
	CHARSET_ANSI,
#endif

#ifdef SCHISM_OS2
	CHARSET_DOSCP,
#endif

	/* Uses the system's value for smSystemScript */
#ifdef SCHISM_MACOS
	CHARSET_SYSTEMSCRIPT,
#endif
	/* END SYSTEM-SPECIFIC HACKS */
} charset_t;

typedef enum {
	CHARSET_ERROR_SUCCESS = 0,
	CHARSET_ERROR_UNIMPLEMENTED = -1,
	CHARSET_ERROR_INPUTISOUTPUT = -2,
	CHARSET_ERROR_NULLINPUT = -3,
	CHARSET_ERROR_NULLOUTPUT = -4,
	CHARSET_ERROR_DECODE = -5,
	CHARSET_ERROR_ENCODE = -6,
	CHARSET_ERROR_NOMEM = -7,
} charset_error_t;

enum {
	DECODER_STATE_INVALID_CHAR = -4, /* character unavailable in destination */
	DECODER_STATE_ILL_FORMED = -3,   /* input buffer is ill-formed */
	DECODER_STATE_OVERFLOWED = -2,   /* reached past input buffer size */
	DECODER_STATE_ERROR = -1,        /* unknown generic decoding error */
	DECODER_STATE_NEED_MORE = 0,     /* needs more bytes */
	DECODER_STATE_DONE = 1,          /* decoding done! */
};

typedef struct {
	/* -- input, set by the caller */
	const unsigned char *in;  /* input buffer */
	size_t size;              /* size of the buffer, can be SIZE_MAX if unknown */
	size_t offset;            /* current decoding offset, should always be set to zero */

	/* -- output, decoder initializes these */
	uint32_t codepoint; /* decoded codepoint if successful, undefined if not */
	int state;          /* one of DECODER_* definitions above; negative values are errors */
} charset_decode_t;

SCHISM_CONST int char_digraph(int k1, int k2);
SCHISM_CONST int char_unicode_to_cp866(uint32_t c);
SCHISM_CONST int char_unicode_to_cp437(uint32_t c);
SCHISM_CONST int char_unicode_to_itf(uint32_t c);

/* ------------------------------------------------------------------------ */

/* unicode composing and case folding graciously provided by utf8proc */
void *charset_compose_to_set(const void *in, charset_t inset, charset_t outset);
void *charset_case_fold_to_set(const void *in, charset_t inset, charset_t outset);

/* only provided for source compatibility and should be killed with fire */
#define charset_compose(in, set)         charset_compose_to_set(in, set, set)
#define charset_compose_to_utf8(in, set) charset_compose_to_set(in, set, CHARSET_UTF8)

#define charset_case_fold(in, set)         charset_case_fold_to_set(in, set, set)
#define charset_case_fold_to_utf8(in, set) charset_case_fold_to_set(in, set, CHARSET_UTF8)

/* ------------------------------------------------------------------------ */

/* charset-aware replacements for C stdlib functions */
size_t charset_strlen(const void* in, charset_t inset);
int32_t charset_strcmp(const void* in1, charset_t in1set, const void* in2, charset_t in2set);
int32_t charset_strncmp(const void* in1, charset_t in1set, const void* in2, charset_t in2set, size_t num);
int32_t charset_strcasecmp(const void* in1, charset_t in1set, const void* in2, charset_t in2set);
int32_t charset_strncasecmp(const void* in1, charset_t in1set, const void* in2, charset_t in2set, size_t num);
size_t charset_strncasecmplen(const void* in1, charset_t in1set, const void* in2, charset_t in2set, size_t num);
int32_t charset_strverscmp(const void *in1, charset_t in1set, const void *in2, charset_t in2set);
int32_t charset_strcaseverscmp(const void *in1, charset_t in1set, const void *in2, charset_t in2set);
void *charset_strstr(const void *in1, charset_t in1set, const void *in2, charset_t in2set);
void *charset_strcasestr(const void *in1, charset_t in1set, const void *in2, charset_t in2set);

/* basic fnmatch */
enum {
	CHARSET_FNM_CASEFOLD = (1 << 0),
	CHARSET_FNM_PERIOD   = (1 << 1),
};

int charset_fnmatch(const void *match, charset_t match_set, const void *str, charset_t str_set, int flags);

/* iconv replacement */
#define CHARSET_NUL_TERMINATED SIZE_MAX /* Use this size if you know the input has a NUL terminator character */

const char* charset_iconv_error_lookup(charset_error_t err);
charset_error_t charset_iconv(const void* in, void* out, charset_t inset, charset_t outset, size_t insize);

/* character-by-character variant of charset_iconv; use as
 *     charset_decode_t decoder = {
 *         .in = buf,
 *         .offset = 0,
 *         .size = size, // can be SIZE_MAX if unknown
 *
 *         .codepoint = 0,
 *         .state = DECODER_STATE_NEED_MORE,
 *     };
 *
 *     while (decoder->state == DECODER_STATE_NEED_MORE && !charset_decode_next(decoder, CHARSET_WHATEVER)) {
 *         // codepoint is in decoder->codepoint
 *     }
*/
charset_error_t charset_decode_next(charset_decode_t *decoder, charset_t inset);

/* charset_iconv for newbies.
 * This is preferred to using the below macro, because it is less prone to memory leaks.
 * Do note that it assumes the input is NUL terminated. */
SCHISM_ALWAYS_INLINE static inline
void *charset_iconv_easy(const void *in, charset_t inset, charset_t outset) {
	void *out;
	if (!charset_iconv(in, &out, inset, outset, SIZE_MAX))
		return out;
	return NULL;
}

#endif /* SCHISM_CHARSET_H_ */
