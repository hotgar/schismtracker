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
#include "fmt.h"
#include "it.h"
#include "disko.h"
#include "player/sndfile.h"
#include "log.h"

// Standard IFF chunks IDs
#define IFFID_FORM UINT32_C(0x464f524d)
#define IFFID_RIFF UINT32_C(0x52494646)
#define IFFID_WAVE UINT32_C(0x57415645)
#define IFFID_LIST UINT32_C(0x4C495354)
#define IFFID_INFO UINT32_C(0x494E464F)

// Wave IFF chunks IDs
#define IFFID_wave UINT32_C(0x77617665)
#define IFFID_fmt  UINT32_C(0x666D7420)
#define IFFID_wsmp UINT32_C(0x77736D70)
#define IFFID_pcm  UINT32_C(0x70636d20)
#define IFFID_data UINT32_C(0x64617461)
#define IFFID_smpl UINT32_C(0x736D706C)
#define IFFID_xtra UINT32_C(0x78747261)

/* --------------------------------------------------------------------------------------------------------- */

int wav_chunk_fmt_read(const void *data, size_t size, void *void_fmt)
{
	struct wave_format *fmt = (struct wave_format *)void_fmt;

	slurp_t fp;
	slurp_memstream(&fp, (uint8_t *)data, size);

#define READ_VALUE(name) \
	do { if (slurp_read(&fp, &name, sizeof(name)) != sizeof(name)) { unslurp(&fp); return 0; } } while (0)

	READ_VALUE(fmt->format);
	READ_VALUE(fmt->channels);
	READ_VALUE(fmt->freqHz);
	READ_VALUE(fmt->bytessec);
	READ_VALUE(fmt->samplesize);
	READ_VALUE(fmt->bitspersample);

	fmt->format        = bswapLE16(fmt->format);
	fmt->channels      = bswapLE16(fmt->channels);
	fmt->freqHz        = bswapLE32(fmt->freqHz);
	fmt->bytessec      = bswapLE32(fmt->bytessec);
	fmt->samplesize    = bswapLE16(fmt->samplesize);
	fmt->bitspersample = bswapLE16(fmt->bitspersample);

	/* BUT I'M NOT DONE YET */
	if (fmt->format == WAVE_FORMAT_EXTENSIBLE) {
		static const unsigned char subformat_base_check[12] = {
			0x00, 0x00, 0x10, 0x00,
			0x80, 0x00, 0x00, 0xAA,
			0x00, 0x38, 0x9B, 0x71,
		};

		uint32_t subformat;
		unsigned char subformat_base[12];
		uint16_t ext_size;

		READ_VALUE(ext_size);
		ext_size = bswapLE16(ext_size);

		if (ext_size < 22)
			return 0;

		slurp_seek(&fp, 6, SEEK_CUR);

		READ_VALUE(subformat);
		READ_VALUE(subformat_base);

		if (memcmp(subformat_base, subformat_base_check, 12))
			return 0;

		fmt->format = bswapLE32(subformat);
	}

#undef READ_VALUE

	return 1;
}

/* --------------------------------------------------------------------------------------------------------- */

static int wav_load(song_sample_t *smp, slurp_t *fp, int load_sample)
{
	iff_chunk_t fmt_chunk = {0}, data_chunk = {0}, smpl_chunk = {0}, xtra_chunk = {0};
	struct wave_format fmt;
	uint32_t flags;

	{
		uint32_t id_RIFF, id_WAVE;

		if (slurp_read(fp, &id_RIFF, sizeof(id_RIFF)) != sizeof(id_RIFF))
			return 0;

		/* skip filesize. */
		slurp_seek(fp, 4, SEEK_CUR);

		if (slurp_read(fp, &id_WAVE, sizeof(id_WAVE)) != sizeof(id_WAVE))
			return 0;

		if (bswapBE32(id_RIFF) != IFFID_RIFF ||
			bswapBE32(id_WAVE) != IFFID_WAVE)
			return 0;
	}

	{
		iff_chunk_t c;

		while (riff_chunk_peek(&c, fp)) {
			switch (c.id) {
			case IFFID_fmt:
				if (fmt_chunk.id)
					return 0;

				fmt_chunk = c;
				break;
			case IFFID_data:
				if (data_chunk.id)
					return 0;

				data_chunk = c;
				break;
			case IFFID_xtra:
				xtra_chunk = c;
				break;
			case IFFID_smpl:
				smpl_chunk = c;
				break;
			default:
				break;
			}
		}
	}

	/* this should never happen */
	if (!fmt_chunk.id || !data_chunk.id)
		return 0;

	/* now we have all the chunks we need. */
	if (!iff_chunk_receive(&fmt_chunk, fp, wav_chunk_fmt_read, &fmt))
		return 0;

	// endianness
	flags = SF_LE;

	// channels
	flags |= (fmt.channels == 2) ? SF_SI : SF_M; // interleaved stereo

	// bit width
	switch (fmt.bitspersample) {
	case 8:  flags |= SF_8;  break;
	case 16: flags |= SF_16; break;
	case 24: flags |= SF_24; break;
	case 32: flags |= SF_32; break;
	default: return 0; // unsupported
	}

	// encoding (8-bit wav is unsigned, everything else is signed -- yeah, it's stupid)
	switch (fmt.format) {
	case WAVE_FORMAT_PCM:
		flags |= (fmt.bitspersample == 8) ? SF_PCMU : SF_PCMS;
		break;
	case WAVE_FORMAT_IEEE_FLOAT:
		flags |= SF_IEEE;
		break;
	default: return 0; // unsupported
	}

	smp->flags         = 0; // flags are set by csf_read_sample
	smp->c5speed       = fmt.freqHz;
	smp->length        = data_chunk.size / ((fmt.bitspersample / 8) * fmt.channels);

	/* if we have XTRA or SMPL chunks, fill them in as well. */
	if (xtra_chunk.id) {
		slurp_seek(fp, xtra_chunk.offset, SEEK_SET);
		iff_read_xtra_chunk(fp, smp);
	}

	if (smpl_chunk.id) {
		slurp_seek(fp, smpl_chunk.offset, SEEK_SET);
		iff_read_smpl_chunk(fp, smp);
	}

	if (load_sample) {
		return iff_read_sample(&data_chunk, fp, smp, flags, 0);
	} else {
		if (fmt.channels == 2)
			smp->flags |= CHN_STEREO;

		if (fmt.bitspersample > 8)
			smp->flags |= CHN_16BIT;
	}

	return 1;
}

/* --------------------------------------------------------------------------------------------------------- */

int fmt_wav_load_sample(slurp_t *fp, song_sample_t *smp)
{
	return wav_load(smp, fp, 1);
}

int fmt_wav_read_info(dmoz_file_t *file, slurp_t *fp)
{
	song_sample_t smp;

	smp.volume = 64 * 4;
	smp.global_volume = 64;

	if (!wav_load(&smp, fp, 0))
		return 0;

	fmt_fill_file_from_sample(file, &smp);

	file->description  = "IBM/Microsoft RIFF Audio";
	file->type         = TYPE_SAMPLE_PLAIN;
	file->smp_filename = file->base;

	return 1;
}

/* --------------------------------------------------------------------------------------------------------- */
/* wav is like aiff's retarded cousin */

struct wav_writedata {
	long data_size; // seek position for writing data size (in bytes)
	size_t numbytes; // how many bytes have been written
	int bps; // bytes per sample
	int swap; // should be byteswapped?
	int bpf; // bytes per frame
};

/* returns bytes per frame */
static int wav_header(disko_t *fp, int bits, int channels, uint32_t rate, size_t length,
	struct wav_writedata *wwd /* out */)
{
	int16_t s;
	uint32_t ul;
	int bps = 1;
	int bpf;

	bps *= ((bits + 7) / 8);
	bpf = bps * channels;

	if (wwd) {
		wwd->bps = bps;
		wwd->bpf = bpf;
	}

	/* write a very large size for now */
	disko_write(fp, "RIFF\377\377\377\377WAVEfmt ", 16);
	ul = bswapLE32(16); // fmt chunk size
	disko_write(fp, &ul, 4);
	s = bswapLE16(1); // linear pcm
	disko_write(fp, &s, 2);
	s = bswapLE16(channels); // number of channels
	disko_write(fp, &s, 2);
	ul = bswapLE32(rate); // sample rate
	disko_write(fp, &ul, 4);
	ul = bswapLE32(bpf * rate); // "byte rate" (why?! I have no idea)
	disko_write(fp, &ul, 4);
	s = bswapLE16(bpf); // (oh, come on! the format already stores everything needed to calculate this!)
	disko_write(fp, &s, 2);
	s = bswapLE16(bits); // bits per sample
	disko_write(fp, &s, 2);

	disko_write(fp, "data", 4);
	if (wwd)
		wwd->data_size = disko_tell(fp);
	ul = bswapLE32(bpf * length);
	disko_write(fp, &ul, 4);

	return bpf;
}

/* len should not include a nul terminator */
static inline void fmt_wav_write_INFO_chunk(disko_t *fp, const char chunk[4], const char *text, uint32_t len)
{
	uint32_t dw;

	disko_write(fp, chunk, 4);

	dw = bswapLE32(len + (len & 1));
	disko_write(fp, &dw, 4);

	disko_write(fp, text, len);

	/* word align; I'm not sure if this is the "correct" way
	 * to do this, but eh */
	if (len & 1) disko_putc(fp, ' ');
}

static void fmt_wav_write_LIST(disko_t *fp, const char *title)
{
	/* this is used to "fix" the */
	int64_t start, end;
	uint32_t dw;

	start = disko_tell(fp);

	disko_write(fp, "LIST", 4);

	disko_seek(fp, 4, SEEK_CUR);

	disko_write(fp, "INFO", 4);

	{
		/* ISFT (Software) chunk */
		const char ver[] = "Schism Tracker " VERSION;

		fmt_wav_write_INFO_chunk(fp, "ISFT", ver, sizeof(ver) - 1);
	}

	if (title && *title) {
		/* INAM (title/name) chunk */
		fmt_wav_write_INFO_chunk(fp, "INAM", title, strlen(title));
	}

	end = disko_tell(fp);

	/* now we can fill in the length */
	disko_seek(fp, start + 4, SEEK_SET);

	dw = bswapLE32(end - start - 8);
	disko_write(fp, &dw, 4);

	/* back to the end */
	disko_seek(fp, 0, SEEK_END);
}

int fmt_wav_save_sample(disko_t *fp, song_sample_t *smp)
{
	int bps;
	uint32_t ul;
	uint32_t flags = SF_LE;

	if (smp->flags & CHN_ADLIB)
		return SAVE_UNSUPPORTED;

	flags |= (smp->flags & CHN_16BIT) ? (SF_16 | SF_PCMS) : (SF_8 | SF_PCMU);
	flags |= (smp->flags & CHN_STEREO) ? SF_SI : SF_M;

	bps = wav_header(fp, (smp->flags & CHN_16BIT) ? 16 : 8, (smp->flags & CHN_STEREO) ? 2 : 1,
		smp->c5speed, smp->length, NULL);

	if (csf_write_sample(fp, smp, flags, UINT32_MAX) != smp->length * bps) {
		log_appendf(4, "WAV: unexpected data size written");
		return SAVE_INTERNAL_ERROR;
	}

	{
		unsigned char data[MAX(IFF_XTRA_CHUNK_SIZE, IFF_SMPL_CHUNK_SIZE)];
		uint32_t length;

		iff_fill_xtra_chunk(smp, data, &length);
		disko_write(fp, data, length);

		iff_fill_smpl_chunk(smp, data, &length);
		disko_write(fp, data, length);
	}

	fmt_wav_write_LIST(fp, smp->name);

	/* fix the length in the file header */
	ul = disko_tell(fp) - 8;
	ul = bswapLE32(ul);
	disko_seek(fp, 4, SEEK_SET);
	disko_write(fp, &ul, 4);

	return SAVE_SUCCESS;
}


int fmt_wav_export_head(disko_t *fp, int bits, int channels, uint32_t rate)
{
	struct wav_writedata *wwd = malloc(sizeof(struct wav_writedata));
	if (!wwd)
		return DW_ERROR;
	fp->userdata = wwd;
	wav_header(fp, bits, channels, rate, ~0, wwd);
	wwd->numbytes = 0;
#if WORDS_BIGENDIAN
	wwd->swap = (bits > 8);
#else
	wwd->swap = 0;
#endif

	return DW_OK;
}

int fmt_wav_export_body(disko_t *fp, const uint8_t *data, size_t length)
{
	struct wav_writedata *wwd = fp->userdata;

	if (fmt_write_pcm(fp, data, length, wwd->bpf, wwd->bps,
			wwd->swap, "WAV") < 0)
		return DW_ERROR;

	wwd->numbytes += length;

	return DW_OK;
}

int fmt_wav_export_silence(disko_t *fp, long bytes)
{
	struct wav_writedata *wwd = fp->userdata;
	wwd->numbytes += bytes;

	disko_seek(fp, bytes, SEEK_CUR);
	return DW_OK;
}

int fmt_wav_export_tail(disko_t *fp)
{
	struct wav_writedata *wwd = fp->userdata;
	uint32_t ul;

	fmt_wav_write_LIST(fp, NULL);

	/* fix the length in the file header */
	ul = disko_tell(fp) - 8;
	ul = bswapLE32(ul);
	disko_seek(fp, 4, SEEK_SET);
	disko_write(fp, &ul, 4);

	/* write the other lengths */
	disko_seek(fp, wwd->data_size, SEEK_SET);
	ul = bswapLE32(wwd->numbytes);
	disko_write(fp, &ul, 4);

	free(wwd);

	return DW_OK;
}
