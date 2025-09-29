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

#include "headers.h"
#include "bits.h"
#include "slurp.h"
#include "fmt.h"
#include "mem.h"

#include "player/sndfile.h"

#include "version.h"
#include "disko.h"
#include "log.h"

/* --------------------------------------------------------------------- */

/* TODO: WOW files */

/* Ugh. */
static const char *valid_tags[][2] = {
	/* M.K. must be the first tag! (to test for WOW files) */
	/* the first 5 descriptions are a bit weird */
	{"M.K.", "Amiga-NewTracker"},
	{"M!K!", "Amiga-ProTracker"},
	{"M&K!", "Amiga-NoiseTracker"},
	{"N.T.", "Amiga-NoiseTracker"},
	{"FEST", "Amiga-NoiseTracker"}, /* jobbig.mod */

	/* Atari Octalyzer */
#define FALCON(x) {"CD" #x "1", #x " Channel Falcon"}
	FALCON(6), FALCON(8),
#undef FALCON

	/* Startrekker (quite rare...) */
#define STRTRK(x) {"FLT" #x, #x " Channel Startrekker"}, {"EXO" #x, #x " Channel Startrekker"}
	STRTRK(4), STRTRK(8),
#undef STRTRK

	/* Oktalyzer */
	{"OCTA", "8 Channel MOD"},
	{"OKTA", "8 Channel MOD"},

#define TDZ(x) {"TDZ" #x, #x " Channel MOD"}
	TDZ(1), TDZ(2), TDZ(3),
#undef TDZ

	/* xCHN = generic */
#define CHN(x) {#x "CHN", #x " Channel MOD"}
	CHN(1), CHN(2), CHN(3), CHN(4),
	CHN(5), CHN(6), CHN(7), CHN(8),
	CHN(9),
#undef CHN

	/* xxCN/xxCH = generic */
#define CN(x) {#x "CN", #x " Channel MOD"}, {#x "CH", #x " Channel MOD"}
	CN(10), CN(11), CN(12), CN(13),
	CN(14), CN(15), CN(16), CN(17),
	CN(18), CN(19), CN(20), CN(21),
	CN(22), CN(23), CN(24), CN(25),
	CN(26), CN(27), CN(28), CN(29),
	CN(30), CN(31), CN(32),
#undef CN
	{NULL, NULL}
};

enum {
	WARN_LINEARSLIDES,
	WARN_SAMPLEVOL,
	WARN_LOOPS,
	WARN_SAMPLEVIB,
	WARN_INSTRUMENTS,
	WARN_PATTERNLEN,
	WARN_NOTERANGE,
	WARN_VOLEFFECTS,
	WARN_MAXSAMPLES,
	WARN_LONGSAMPLES,
	WARN_UNUSEDPATS,

	MAX_WARN
};

static const char *mod_warnings[] = {
	[WARN_LINEARSLIDES] = "Linear slides",
	[WARN_SAMPLEVOL]    = "Sample volumes",
	[WARN_LOOPS]        = "Sustain and Ping Pong loops",
	[WARN_SAMPLEVIB]    = "Sample vibrato",
	[WARN_INSTRUMENTS]  = "Instrument functions",
	[WARN_PATTERNLEN]   = "Pattern lengths other than 64 rows",
	[WARN_NOTERANGE]    = "Notes outside the range C-4 to B-6",
	[WARN_VOLEFFECTS]   = "Extended volume column effects",
	[WARN_MAXSAMPLES]   = "Over 31 samples",
	[WARN_LONGSAMPLES]  = "Odd sample length or greater than 131070",
	[WARN_UNUSEDPATS]   = "Patterns outside order list",

	[MAX_WARN]          = NULL
};

const uint16_t amigaperiod_table[256] = {
	0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,   0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,   0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,   0,    0,    0,
	1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 906,
	856,  808,  762,  720,  678,  640,  604,  570,  538,  508,  480, 453,
	428,  404,  381,  360,  339,  320,  302,  285,  269,  254,  240, 226,
	214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120, 113,
	107,  101,  95,   90,   85,   80,   75,   71,   67,   63,   60,  56,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
	0,    0,    0
};

int fmt_mod_read_info(dmoz_file_t *file, slurp_t *fp)
{
	char tag[4], title[20];
	int i = 0;

	if (slurp_length(fp) < 1085)
		return 0;

	if (slurp_read(fp, title, sizeof(title)) != sizeof(title))
		return 0;

	slurp_seek(fp, 1080, SEEK_SET);
	if (slurp_read(fp, tag, sizeof(tag)) != sizeof(tag))
		return 0;

	for (i = 0; valid_tags[i][0] != NULL; i++) {
		if (memcmp(tag, valid_tags[i][0], 4) == 0) {
			/* if (i == 0) {
				Might be a .wow; need to calculate some crap to find out for sure.
				For now, since I have no wow's, I'm not going to care.
			} */

			file->description = valid_tags[i][1];
			/*file->extension = str_dup("mod");*/
			file->title = strn_dup(title, sizeof(title));
			file->type = TYPE_MODULE_MOD;
			return 1;
		}
	}

	/* check if it could be a SoundTracker MOD */
	slurp_rewind(fp);
	int errors = 0;
	for (i = 0; i < 20; i++) {
		int b = slurp_getc(fp);
		if (b > 0 && b < 32) {
			errors++;
			if (errors > 5)
				return 0;
		}
	}

	uint8_t all_volumes = 0, all_lengths = 0;
	for (i = 0; i < 15; i++) {
		slurp_seek(fp, 20 + i * 30 + 22, SEEK_SET);
		int length_high = slurp_getc(fp);
		int length_low = slurp_getc(fp);
		int length = length_high * 0x100 + length_low;
		int finetune = slurp_getc(fp);
		int volume = slurp_getc(fp);

		if (finetune)
			return 0; /* invalid finetune */

		if (volume > 64)
			return 0; /* invalid volume */

		if (length > 32768)
			return 0; /* invalid sample length */

		all_volumes |= volume;
		all_lengths |= length_high | length_low;
	}

	if (!all_lengths || !all_volumes)
		return 0;

	file->description = "SoundTracker";
	/*file->extension = str_dup("mod");*/
	file->title = strn_dup(title, sizeof(title));
	file->type = TYPE_MODULE_MOD;

	return 1;
}

/* --------------------------------------------------------------------------------------------------------- */

/* force determines whether the loader will force-read untagged files as
   15-sample mods */
static int fmt_mod_load_song(song_t *song, slurp_t *fp, uint32_t lflags, int force)
{
	uint8_t tag[4];
	int n, npat, pat, chan, nchan, nord;
	song_note_t *note;
	uint16_t tmp;
	int startrekker = 0;
	int test_wow = 0;
	int mk = 0;
	int maybe_st3 = 0;
	int maybe_ft2 = 0;
	int his_masters_noise = 0;
	uint8_t restart;
	long samplesize = 0;
	const char *tid = NULL;
	int nsamples = 31; /* default; tagless mods have 15 */

	/* check the tag (and set the number of channels) -- this is ugly, so don't look */
	slurp_seek(fp, 1080, SEEK_SET);
	slurp_read(fp, tag, 4);
	if (!memcmp(tag, "M.K.", 4)) {
		/* M.K. = Protracker etc., or Mod's Grave (*.wow) */
		nchan = 4;
		test_wow = 1;
		mk = 1;
		maybe_ft2 = 1;
		tid = "Amiga-NewTracker";
	} else if (!memcmp(tag, "M!K!", 4)) {
		nchan = 4;
		tid = "Amiga-ProTracker";
	} else if (!memcmp(tag, "M&K!", 4) || !memcmp(tag, "N.T.", 4) || !memcmp(tag, "FEST", 4)) {
		nchan = 4;
		tid = "Amiga-NoiseTracker";
		if (!memcmp(tag, "M&K!", 4) || !memcmp(tag, "FEST", 4)) {
			// Alternative finetuning
			his_masters_noise = 1;
		}
	} else if ((!memcmp(tag, "FLT", 3) || !memcmp(tag, "EXO", 3)) && (tag[3] == '4' || tag[3] == '8')) {
		// Hopefully EXO8 is stored the same way as FLT8
		nchan = tag[3] - '0';
		startrekker = (nchan == 8);
		tid = "%d Channel Startrekker";
		//log_appendf(4, " Warning: Startrekker AM synth is not supported");
	} else if (!memcmp(tag, "OCTA", 4) || !memcmp(tag, "OKTA", 4)) {
		nchan = 8;
		tid = "Amiga Oktalyzer"; // IT just identifies this as "8 Channel MOD"
	} else if (!memcmp(tag, "CD61", 4) || !memcmp(tag, "CD81", 4)) {
		nchan = 8;
		tid = "8 Channel Falcon"; // Atari Oktalyser
	} else if (tag[0] > '0' && tag[0] <= '9' && !memcmp(tag + 1, "CHN", 3)) {
		/* nCHN = Fast Tracker (if n is even) or TakeTracker (if n = 5, 7, or 9) */
		nchan = tag[0] - '0';
		if (nchan == 5 || nchan == 7 || nchan == 9) {
			tid = "%d Channel TakeTracker";
		} else {
			if (!(nchan & 1))
				maybe_ft2 = 1;
			tid = "%d Channel MOD"; // generic
		}
		maybe_st3 = 1;
	} else if (tag[0] > '0' && tag[0] <= '9' && tag[1] >= '0' && tag[1] <= '9'
		   && tag[2] == 'C' && (tag[3] == 'H' || tag[3] == 'N')) {
		/* nnCH = Fast Tracker (if n is even and <= 32) or TakeTracker (if n = 11, 13, 15)
		 * Not sure what the nnCN variant is. */
		nchan = 10 * (tag[0] - '0') + (tag[1] - '0');
		if (nchan == 11 || nchan == 13 || nchan == 15) {
			tid = "%d Channel TakeTracker";
		} else {
			if ((nchan & 1) == 0 && nchan <= 32 && tag[3] == 'H')
				maybe_ft2 = 1;
			tid = "%d Channel MOD"; // generic
		}
		if (tag[3] == 'H')
			maybe_st3 = 1;
	} else if (!memcmp(tag, "TDZ", 3) && tag[3] > '0' && tag[3] <= '9') {
		/* TDZ[1-3] = TakeTracker */
		nchan = tag[3] - '0';
		if (nchan < 4)
			tid = "%d Channel TakeTracker";
		else
			tid = "%d Channel MOD";
	} else if (force) {
		/* some old modules don't have tags, so try loading anyway */
		nchan = 4;
		nsamples = 15;
		tid = "%d Channel MOD";
	} else {
		return LOAD_UNSUPPORTED;
	}

	/* suppose the tag is 90CH :) */
	if (nchan > MAX_CHANNELS) {
		//fprintf(stderr, "%s: Too many channels!\n", filename);
		return LOAD_FORMAT_ERROR;
	}

	/* read the title */
	slurp_rewind(fp);
	slurp_read(fp, song->title, 20);
	song->title[20] = 0;

	/* sample headers */
	for (n = 1; n < nsamples + 1; n++) {
		if (slurp_read(fp, song->samples[n].name, 22) != 22)
			return LOAD_UNSUPPORTED;

		song->samples[n].name[22] = 0;

		if (slurp_read(fp, &tmp, 2) != 2)
			return LOAD_UNSUPPORTED;

		song->samples[n].length = bswapBE16(tmp) * 2;

		/* this is only necessary for the wow test... */
		samplesize += song->samples[n].length;

		if (his_masters_noise) {
			song->samples[n].c5speed = transpose_to_frequency(0, -(signed char)(slurp_getc(fp) << 3));
		} else {
			song->samples[n].c5speed = MOD_FINETUNE(slurp_getc(fp));
		}

		song->samples[n].volume = slurp_getc(fp);
		if (song->samples[n].volume > 64)
			song->samples[n].volume = 64;
		if (!song->samples[n].length && song->samples[n].volume)
			maybe_ft2 = 0;
		song->samples[n].volume *= 4; //mphack
		song->samples[n].global_volume = 64;

		if (slurp_read(fp, &tmp, 2) != 2)
			return LOAD_UNSUPPORTED;

		song->samples[n].loop_start = bswapBE16(tmp) * 2;
		if (slurp_read(fp, &tmp, 2) != 2)
			return LOAD_UNSUPPORTED;

		tmp = bswapBE16(tmp) * 2;
		if (tmp > 2)
			song->samples[n].flags |= CHN_LOOP;
		else if (tmp == 0)
			maybe_st3 = 0;
		else if (!song->samples[n].length)
			maybe_ft2 = 0;
		song->samples[n].loop_end = song->samples[n].loop_start + tmp;
		song->samples[n].vib_type = 0;
		song->samples[n].vib_rate = 0;
		song->samples[n].vib_depth = 0;
		song->samples[n].vib_speed = 0;
	}

	/* pattern/order stuff */
	nord = slurp_getc(fp);
	restart = slurp_getc(fp);

	if (slurp_read(fp, song->orderlist, 128) != 128)
		return LOAD_UNSUPPORTED;

	npat = 0;
	if (startrekker) {
		/* from mikmod: if the file says FLT8, but the orderlist
		has odd numbers, it's probably really an FLT4 */
		for (n = 0; n < 128; n++) {
			if (song->orderlist[n] & 1) {
				startrekker = 0;
				nchan = 4;
				break;
			}
		}
	}

	if (startrekker) {
		for (n = 0; n < 128; n++)
			song->orderlist[n] >>= 1;
	}

	for (n = 0; n < 128; n++) {
		if (song->orderlist[n] >= MAX_PATTERNS)
			song->orderlist[n] = ORDER_SKIP;
		else if (song->orderlist[n] > npat)
			npat = song->orderlist[n];
	}

	/* set all the extra orders to the end-of-song marker */
	memset(song->orderlist + nord, ORDER_LAST, MAX_ORDERS - nord);

	if (restart == 0x7f && maybe_st3)
		tid = "Scream Tracker 3?";
	else if (restart == 0x7f && mk)
		tid = "%d Channel ProTracker";
	else if (restart <= npat && maybe_ft2)
		tid = "%d Channel FastTracker";
	else if (restart == npat && mk)
		tid = "%d Channel Soundtracker";

	/* hey, is this a wow file? */
	if (test_wow) {
		slurp_seek(fp, 0, SEEK_END);
		if (slurp_tell(fp) == 2048 * npat + samplesize + 3132) {
			nchan = 8;
			tid = "Mod's Grave WOW";
		}
	}

	/* 15-sample mods don't have a 4-byte tag... or the other 16 samples */
	slurp_seek(fp, nsamples == 15 ? 600 : 1084, SEEK_SET);

	/* pattern data */
	if (startrekker) {
		for (pat = 0; pat <= npat; pat++) {
			note = song->patterns[pat] = csf_allocate_pattern(64);
			song->pattern_size[pat] = song->pattern_alloc_size[pat] = 64;
			for (n = 0; n < 64; n++, note += 60) {
				for (chan = 0; chan < 4; chan++, note++) {
					uint8_t p[4];
					if (slurp_read(fp, p, 4) != 4)
						return LOAD_UNSUPPORTED;
					mod_import_note(p, note);
					csf_import_mod_effect(note, 0);
				}
			}
			note = song->patterns[pat] + 4;
			for (n = 0; n < 64; n++, note += 60) {
				for (chan = 0; chan < 4; chan++, note++) {
					uint8_t p[4];
					if (slurp_read(fp, p, 4) != 4)
						return LOAD_UNSUPPORTED;
					mod_import_note(p, note);
					csf_import_mod_effect(note, 0);
				}
			}
		}
	} else {
		for (pat = 0; pat <= npat; pat++) {
			note = song->patterns[pat] = csf_allocate_pattern(64);
			song->pattern_size[pat] = song->pattern_alloc_size[pat] = 64;
			for (n = 0; n < 64; n++, note += 64 - nchan) {
				for (chan = 0; chan < nchan; chan++, note++) {
					uint8_t p[4];
					if (slurp_read(fp, p, 4) != 4)
						return LOAD_UNSUPPORTED;
					mod_import_note(p, note);
					csf_import_mod_effect(note, 0);
				}
			}
		}
	}

	if (restart < npat)
		csf_insert_restart_pos(song, restart);

	{
		/* "TakeTrackered with version 0.9E!!!!!" XOR with 0xDF. */
		static const unsigned char taketracker[] = {
			0x8B, 0xBE, 0xB4, 0xBA, 0x8B, 0xAD, 0xBE, 0xBC,
			0xB4, 0xBA, 0xAD, 0xBA, 0xBB, 0xFF, 0xA8, 0xB6,
			0xAB, 0xB7, 0xFF, 0xA9, 0xBA, 0xAD, 0xAC, 0xB6,
			0xB0, 0xB1, 0xFF, 0xEF, 0xF1, 0xE6, 0xBA, 0xFE,
			0xFE, 0xFE, 0xFE, 0xFE,
		};

		/* This is actually nine bytes, but the final three vary between
		 * Tetramed versions.
		 * Possibly they could be used to fingerprint versions?? */
		static const unsigned char tetramed[] = {0x00, 0x11, 0x55, 0x33, 0x22, 0x11};

		unsigned char magicEOF[MAX(sizeof(taketracker), sizeof(tetramed) + 3u)];
		size_t len;

		for (n = 1; n < nsamples + 1; n++) {
			if (song->samples[n].length == 0)
				continue;

			/* check for ADPCM compression */
			uint32_t flags = SF_8 | SF_M | SF_LE;
			unsigned char sstart[5];
			slurp_peek(fp, sstart, sizeof(sstart));
			if (!memcmp(sstart, "ADPCM", sizeof(sstart))) {
				slurp_seek(fp, sizeof(sstart), SEEK_CUR);
				flags |= SF_PCMD16;
			} else {
				flags |= SF_PCMS;
			}

			if (lflags & LOAD_NOSAMPLES) {
				/* just skip the data, I guess */
				slurp_seek(fp, song->samples[n].length, SEEK_CUR);
			} else {
				csf_read_sample(song->samples + n, flags, fp);
			}
		}

		len = slurp_read(fp, magicEOF, sizeof(magicEOF));

		/* Some trackers dump extra data at the end of the file. */
		if (nchan <= 16 && len >= sizeof(taketracker) && !memcmp(taketracker, magicEOF, sizeof(taketracker))) {
			tid = "%d Channel TakeTracker";
		} else if (mk && len >= (sizeof(tetramed) + 3u) && !memcmp(tetramed, magicEOF, sizeof(tetramed))) {
			tid = "%d Channel Tetramed";
		}
#if 0
		else if (len >= 0) {
			size_t z;
			for (z = 0; z < len; z++)
				printf("%02x ", magicEOF[z]);
		}
#endif
	}

	/* set some other header info that's always the same for .mod files */
	song->flags = (SONG_ITOLDEFFECTS | SONG_COMPATGXX);
	for (n = 0; n < nchan; n++)
		song->channels[n].panning = PROTRACKER_PANNING(n);
	for (; n < MAX_CHANNELS; n++)
		song->channels[n].flags = CHN_MUTE;

	song->pan_separation = 64;

	snprintf(song->tracker_id, sizeof(song->tracker_id), tid ? tid : "%d Channel MOD", nchan);

	/* done! */
	return LOAD_SUCCESS;
}

/* loads everything but old 15-instrument mods... yes, even FLT8 and WOW files
   (and the definition of "everything" is always changing) */
int fmt_mod31_load_song(song_t *song, slurp_t *fp, uint32_t lflags)
{
	return fmt_mod_load_song(song, fp, lflags, 0);
}

/* loads everything including old 15-instrument mods. this is a separate
   function so that it can be called later in the format-checking sequence. */
int fmt_mod15_load_song(song_t *song, slurp_t *fp, uint32_t lflags)
{
	return fmt_mod_load_song(song, fp, lflags, 1);
}

/* .MOD saving routines */
int fmt_mod_save_song(disko_t *fp, song_t *song)
{
	uint8_t mod_orders[128] = {0};
	uint8_t tmp[128];
		
	int nord, nsmp, nchn, maxpat, jmax, joutpos;
	long tmppos;
	int i, j, n, period;
	uint32_t warn = 0;

	if (song->flags & SONG_INSTRUMENTMODE)
		warn |= 1 << WARN_INSTRUMENTS;
	if (song->flags & SONG_LINEARSLIDES)
		warn |= 1 << WARN_LINEARSLIDES;

	nsmp = csf_get_num_samples(song);
	if (nsmp > 31) {
		printf("%d\n", nsmp);
		nsmp = 31;
		warn |= 1 << WARN_MAXSAMPLES;
	}

	nchn = csf_get_highest_used_channel(song) + 1;

	SCHISM_STATIC_ASSERT(sizeof(song->title) >= 20, "song->title is assumed to be at least 20 bytes long");
	disko_write(fp, song->title, 20);

	// Now writing sample headers
	for (n = 1; n <= 31; ++n) {
		uint16_t w;

		if (n > nsmp) {
			disko_seek(fp, 30, SEEK_CUR);
			continue;
		}

		if (song->samples[n].global_volume != 64)
			warn |= 1 << WARN_SAMPLEVOL;
		if ((song->samples[n].flags & (CHN_LOOP | CHN_PINGPONGLOOP)) == (CHN_LOOP | CHN_PINGPONGLOOP) || (song->samples[n].flags & CHN_SUSTAINLOOP))
			warn |= 1 << WARN_LOOPS;
		if (song->samples[n].vib_depth != 0)
			warn |= 1 << WARN_SAMPLEVIB;
		/* these should be separate warnings. */
		if ((1 & song->samples[n].length) || (song->samples[n].length > 0x1FFFE))
			warn |= 1 << WARN_LONGSAMPLES;

		SCHISM_STATIC_ASSERT(sizeof(song->samples[n].name) >= 22, "song->samples[n].name is assumed to be at least 22 bytes long");
		disko_write(fp, song->samples[n].name, 22);

		/* sample length. */
		w = MIN(song->samples[n].length >> 1, 0xFFFF);
		w = bswapBE16(w);
		disko_write(fp, &w, 2);

		/* ...this seems rather stupid. why aren't we just finding the
		 * value with the least difference? */
		for (j = 15; j && (finetune_table[j] > song->samples[n].c5speed); --j)
			if ((song->samples[n].c5speed > 10000) && (j == 8))
				break; /* determine from finetune_table entry */

		disko_putc(fp, (j ^ 8) & 0x0f); /* finetune value */
		disko_putc(fp, (song->samples[n].volume + 1) / 4); /* volume, 0..64 */

		if (song->samples[n].flags & CHN_LOOP) {
			w = MIN(song->samples[n].loop_start >> 1, 0xFFFF);
			w = bswapBE16(w);
			disko_write(fp, &w, 2);

			w = MIN((song->samples[n].loop_end - song->samples[n].loop_start) >> 1, 0xFFFF);
			w = bswapBE16(w);
			disko_write(fp, &w, 2);
		} else {
			disko_write(fp, "\x00\x00\x00\x01", 4);
		}
	}

	tmp[0] = nord = csf_get_num_orders(song);
	tmp[1] = 0x7f;
	disko_write(fp, tmp, 2);

	for (maxpat = i = 0; (i < nord) && (i < 128); ++i) {
		mod_orders[i] = song->orderlist[i];
		maxpat = MAX(maxpat, mod_orders[i]);
	}
	if (maxpat + 1 < csf_get_num_patterns(song))
		warn |= 1 << WARN_UNUSEDPATS;

	disko_write(fp, mod_orders, 128);

	if (nchn == 4) {
		disko_write(fp, (maxpat < 64) ? "M.K." : "M!K!", 4);
	} else {
		unsigned char tag[4];

		if (nchn >= 10) {
			tag[0] = (nchn / 10) + '0';
			tag[1] = (nchn % 10) + '0';
			tag[2] = 'C';
			tag[3] = 'N';
		} else {
			tag[0] = nchn + '0';
			tag[1] = 'C';
			tag[2] = 'H';
			tag[3] = 'N';
		}

		/* guten tag */
		disko_write(fp, tag, 4);
	}

	for(n = 0; n <= maxpat; ++n) {
		/* this is a 16KiB stack variable. we REALLY ought to be
		 * putting stuff directly into the file especially because
		 * most of this array won't even be touched normally */
		uint8_t mod_pattern[MAX_CHANNELS * 4 * 64];
		song_note_t *m;

		memset(mod_pattern, 0, nchn * 4 * 64);
		m = song->patterns[n];
		jmax = song->pattern_size[n];
		if (jmax != 64) {
			jmax = MIN(jmax, 64);
			warn |= 1 << WARN_PATTERNLEN;
		}
		jmax *= MAX_CHANNELS;
		for (j = joutpos = 0; j < jmax; ++j, ++m) {
			uint8_t mod_fx, mod_fx_val;
			if ((j % MAX_CHANNELS) < nchn) {
				period = amigaperiod_table[(m->note) & 0xff];
				if (((m->note) & 0xff) && ((period < 113) || (period > 856)))
					warn |= 1 << WARN_NOTERANGE;

				mod_pattern[joutpos] = ((m->instrument) & 0x10) | (period >> 8);
				mod_pattern[joutpos + 1] = period & 0xff;
				mod_pattern[joutpos + 2] = (m->instrument & 0xf) << 4;

				mod_fx = 0;
				mod_fx_val = m->param;

				if (m->voleffect == VOLFX_VOLUME) {
					mod_fx = 0x0c;
					mod_fx_val = m->volparam;
				} else if (m->voleffect == VOLFX_NONE) {
					switch (m->effect) {
					case FX_NONE: mod_fx_val = 0; break;
					case FX_ARPEGGIO: mod_fx = 0; break;
					case FX_PORTAMENTOUP:
						mod_fx = 1;
						if ((mod_fx_val & 0xf0) == 0xe0) {
							mod_fx = 0x0e;
							mod_fx_val = 0x10 | ((mod_fx_val & 0xf) >> 2);
						} else if ((mod_fx_val & 0xf0) == 0xf0) {
							mod_fx = 0x0e;
							mod_fx_val = 0x10 | (mod_fx_val & 0xf);
						}
						break;
					case FX_PORTAMENTODOWN:
						mod_fx = 2;
						if ((mod_fx_val & 0xf0) == 0xe0) {
							mod_fx = 0x0e;
							mod_fx_val = 0x20 | ((mod_fx_val & 0xf) >> 2);
						} else if ((mod_fx_val & 0xf0) == 0xf0) {
							mod_fx = 0x0e;
							mod_fx_val = 0x20 | (mod_fx_val & 0xf);
						}
						break;
					case FX_TONEPORTAMENTO: mod_fx = 3; break;
					case FX_VIBRATO: mod_fx = 4; break;
					case FX_TONEPORTAVOL: mod_fx = 5; break;
					case FX_VIBRATOVOL: mod_fx = 6; break;
					case FX_TREMOLO: mod_fx = 7; break;
					case FX_PANNING: mod_fx = 8; break;
					case FX_OFFSET: mod_fx = 9; break;
					case FX_VOLUMESLIDE:
						mod_fx = 0x0a;
						if( (mod_fx_val & 0xf0) && (mod_fx_val & 0x0f) ) {
							if ((mod_fx_val & 0xf0) == 0xf0) { // fine volslide down!
								mod_fx = 0x0e;
								mod_fx_val &= 0xbf;
							} else if ((mod_fx_val & 0x0f) == 0x0f) { // fine volslide up!
								mod_fx = 0x0e;
								mod_fx_val = 0xa0 | (mod_fx_val >> 4);
							}
						}
						break;
					case FX_POSITIONJUMP: mod_fx = 0x0b; break;
					case FX_VOLUME: mod_fx = 0x0c; break;
					case FX_PATTERNBREAK: mod_fx = 0x0d; mod_fx_val = ((mod_fx_val / 10) << 4) | (mod_fx_val % 10); break;
					case FX_SPEED: mod_fx = 0x0f; break;
					case FX_TEMPO: mod_fx = 0x0f; break;
					case FX_SPECIAL:
						mod_fx = 0x0e;
						switch (mod_fx_val & 0xf0) {
							case 0x10: mod_fx_val = (mod_fx_val & 0x0f) | 0x30; break;
							case 0x20: mod_fx_val = (mod_fx_val & 0x0f) | 0x50; break; // there is an error in Protracker 2.1 docs!
							case 0x30: mod_fx_val = (mod_fx_val & 0x0f) | 0x40; break;
							case 0x40: mod_fx_val = (mod_fx_val & 0x0f) | 0x70; break;
							case 0xb0: mod_fx_val = (mod_fx_val & 0x0f) | 0x60; break;
							default: break; // handling silently E0x,E6x,E8x,ECx,EDx,EEx,(?EFx)
						}
						break;
					case FX_RETRIG: mod_fx = 0x0e; mod_fx_val = 0x90 | (mod_fx_val & 0x0f); break;
					default:
						warn |= 1 << WARN_VOLEFFECTS;
						break;
					}
				} else {
					/* TODO: try harder */
					warn |= 1 << WARN_VOLEFFECTS;
				}
				mod_pattern[joutpos + 2] |= mod_fx & 0x0f;
				mod_pattern[joutpos + 3] = mod_fx_val;
				joutpos += 4;
			}
		}
		disko_write(fp, mod_pattern, nchn * 64 * 4);
	}

	// Now writing sample data
	for (tmp[0] = tmp[1] = n = 0; (n < nsmp) && (n < 31); ++n) {
		song_sample_t *smp = song->samples + (n + 1);
		if (smp->data) {
			if( (smp->flags & CHN_LOOP) && (smp->loop_start < smp->loop_end) && (smp->loop_end <= MIN(smp->length, 0x1FFFE)) ) {
				csf_write_sample(fp, smp, SF(PCMS,8,M,LE), 0x1FFFE);
			} else if (smp->length >= 1) { // floor(smp->length / 2) MUST be positive!
				tmppos = disko_tell(fp);
				csf_write_sample(fp, smp, SF(PCMS,8,M,LE), 0x1FFFE);
				disko_seek(fp, tmppos, SEEK_SET);
				disko_write(fp, tmp, 2);
				disko_seek(fp, 0, SEEK_END);
			}
		}
	}

	/* announce all the things we broke - ripped from s3m.c */
	for (n = 0; n < MAX_WARN; ++n) {
		if (warn & (1 << n))
			log_appendf(4, " Warning: %s unsupported in MOD format", mod_warnings[n]);
	}

	return SAVE_SUCCESS;
}
