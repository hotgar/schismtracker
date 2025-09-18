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
#include "fmt.h"
#include "bits.h"
#include "log.h"
#include "str.h"
#include "mem.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

/* avio buffer size, in bytes */
#define SCHISM_AVFORMAT_BUFFER_SIZE 65536

/* support for ffmpeg's libavformat. this gives us basically any file format
 * imaginable :) */

static AVIOContext *(*schism_avio_alloc_context)(unsigned char *, int, int,
	void *, int (*)(void *, uint8_t *, int),
	int (*)(void *, const uint8_t *, int),
	int64_t (*)(void *, int64_t, int));
static void *(*schism_av_malloc)(size_t);
static void (*schism_av_free)(void *);
static AVFormatContext *(*schism_avformat_alloc_context)(void);
static void (*schism_avformat_free_context)(AVFormatContext *);
static int (*schism_avformat_open_input)(AVFormatContext **, const char *,
	const AVInputFormat *, AVDictionary **);
static void (*schism_avformat_close_input)(AVFormatContext **);
static int (*schism_avformat_find_stream_info)(AVFormatContext *,
	AVDictionary **);
static void (*schism_av_log_set_callback)(void (*)(void *, int, const char *, va_list));
static const AVCodec *(*schism_avcodec_find_decoder)(enum AVCodecID id);
static AVCodecContext *(*schism_avcodec_alloc_context3)(const AVCodec *codec);
static int (*schism_avcodec_parameters_to_context)(AVCodecContext *codec, const struct AVCodecParameters *par);
static int (*schism_avcodec_open2)(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options);

static AVPacket *(*schism_av_packet_alloc)(void);
static void (*schism_av_packet_free)(AVPacket **pkt);
static AVFrame *(*schism_av_frame_alloc)(void); 
static void (*schism_av_frame_free)(AVFrame **frame);
static void (*schism_av_packet_unref)(AVPacket *pkt);
static int (*schism_av_read_frame)(AVFormatContext *s, AVPacket *pkt);
static int (*schism_avcodec_send_packet)(AVCodecContext *avctx, const AVPacket *avpkt);	
static int (*schism_avcodec_receive_frame)(AVCodecContext *avctx, AVFrame *frame);
static int (*schism_av_get_bytes_per_sample)(enum AVSampleFormat sample_fmt);
static void (*schism_avcodec_free_context)(AVCodecContext **avctx);
static void (*schism_avio_context_free)(AVIOContext **s);

static AVDictionaryEntry *(*schism_av_dict_get)(const AVDictionary *, const char *, const AVDictionaryEntry *, int flags);

static int avformat_wasinit = 0;

static uint32_t schism_avfmt_get_length_estimate(AVFormatContext *fmtctx, int astr)
{
	uint64_t length;
	double duration_secs;

	/* Okay, some notes:
	 *
	 *  1. Most files get the stream duration filled in. At least pretty much
	 *     everything in my media file server has it all filled in.
	 *  2. There are some files that do not fill in stream durations, but DO fill
	 *     in the duration in the format context. FFmpeg documentation states that
	 *     this should always be the case; as in the stream duration and format
	 *     duration are mutually exclusive members. I can only find one batch of
	 *     files that fit this description, which is my copy of Azumanga Daioh
	 *     (AVI vhsrip, MP3 audio codec).
	 *  3. There are probably other cases that I have not handled here.
	 *
	 * That being said, obviously this duration is not exact, and only an estimate.
	 * Exact results can only be obtained by decoding the entire file(!), which we
	 * don't do for obvious reasons :)
	 *   --paper */

	if (fmtctx->streams[astr]->duration > 0) {
		/* log_appendf(1, "FFMPEG: using stream duration"); */

		duration_secs = (double)fmtctx->streams[astr]->duration
			* fmtctx->streams[astr]->time_base.num
			/ fmtctx->streams[astr]->time_base.den;

		length = duration_secs * fmtctx->streams[astr]->codecpar->sample_rate;
	} else if (fmtctx->duration > 0) {
		/* log_appendf(1, "FFMPEG: using format duration"); */

		duration_secs = (double)fmtctx->duration / AV_TIME_BASE;

		length = duration_secs * fmtctx->streams[astr]->codecpar->sample_rate;
	} else {
		/* log_appendf(1, "FFMPEG: using frames * frame size"); */

		/* last resort */
		length = fmtctx->streams[astr]->nb_frames * fmtctx->streams[astr]->codecpar->frame_size;
	}

	return MIN(length, MAX_SAMPLE_LENGTH);
}

/* custom logging callback; we print stuff to the schism-log ;) */

static void schism_av_vlog(void *ptr, int level, const char *fmt, va_list ap)
{
	uint8_t color;
	AVClass *avc;
	char *s;

	avc = ptr ? *(AVClass **)ptr : NULL;

	switch (level) {
	case AV_LOG_FATAL: SCHISM_FALLTHROUGH;
	case AV_LOG_ERROR: color = 4; break;
	case AV_LOG_WARNING: color = 5; break;
	case AV_LOG_INFO: color = 2; break;
	/*
	case AV_LOG_PANIC: -- supposedly this is for crashes?
	case AV_LOG_VERBOSE: -- excessive verboseness
	case AV_LOG_DEBUG: -- only useful for libav* devs
	*/
	default: return;
	}

	if (vasprintf(&s, fmt, ap) < 0)
		return; /* ??? */

	/* cut off any excess */
	str_rtrim(s);

	if (ptr) {
		log_appendf(color, " FFMPEG: %s -- %s", avc->item_name(ptr), s);
	} else {
		log_appendf(color, " FFMPEG: %s", s);
	}

	free(s);
}

static int avfmt_read_packet(void *opaque, uint8_t *buf, int buf_size)
{
	slurp_t *s = opaque;
	size_t r;

	/* weird, EOF gets unset after read ??????????
	 * (this shouldn't happen, but whatever) */
	if (slurp_eof(s))
		return AVERROR_EOF;

	/* cast to int is safe here */
	r = slurp_read(s, buf, buf_size);
	if (!r)
		return (slurp_eof(s) ? AVERROR_EOF : AVERROR_EXTERNAL);

	return r;
}

static int64_t avfmt_seek(void *opaque, int64_t offset, int whence)
{
	slurp_t *s = opaque;
	int r;

	if (whence == AVSEEK_SIZE)
		return slurp_length(s);

	/* ignore this stupid flag */
	whence &= ~(AVSEEK_FORCE);

	switch (whence) {
	case SEEK_SET:
	case SEEK_CUR:
	case SEEK_END:
		break;
	default:
		return -1; /* nope */
	}

	r = slurp_seek(s, offset, whence);

	return (r < 0) ? AVERROR_EXTERNAL : slurp_tell(s);
}

static int sample_fmt_to_sfflags(enum AVSampleFormat fmt, int channels, uint32_t *flags)
{
	int split = 0;

	*flags = 0;

	switch (fmt) {
	case AV_SAMPLE_FMT_U8P: split = 1; SCHISM_FALLTHROUGH;
	case AV_SAMPLE_FMT_U8: *flags |= SF_8 | SF_PCMU; break;

	case AV_SAMPLE_FMT_S16P: split = 1; SCHISM_FALLTHROUGH;
	case AV_SAMPLE_FMT_S16: *flags |= SF_16 | SF_PCMS; break;

	case AV_SAMPLE_FMT_S32P: split = 1; SCHISM_FALLTHROUGH;
	case AV_SAMPLE_FMT_S32: *flags |= SF_32 | SF_PCMS; break;
#if 0
	/* TODO */
	case AV_SAMPLE_FMT_S64P: split = 1; SCHISM_FALLTHROUGH;
	case AV_SAMPLE_FMT_S64: *flags |= SF_64 | SF_PCMS; break;
#endif
	case AV_SAMPLE_FMT_FLTP: split = 1; SCHISM_FALLTHROUGH;
	case AV_SAMPLE_FMT_FLT: *flags |= SF_32 | SF_IEEE; break;

	case AV_SAMPLE_FMT_DBLP: split = 1; SCHISM_FALLTHROUGH;
	case AV_SAMPLE_FMT_DBL: *flags |= SF_64 | SF_IEEE; break;

	default: return 0;
	}

	switch (channels) {
	case 1:
		*flags |= SF_M;
		break;
	case 2:
		*flags |= (split ? SF_SS : SF_SI);
		break;
	default:
		return 0;
	}

#ifdef WORDS_BIGENDIAN
	/* XXX we ought to have an SF_NE (for native endian)
	 * so that this crap isn't sprinkled everywhere */
	*flags |= SF_BE;
#else
	*flags |= SF_LE;
#endif

	return 1;
}



static int avfmt_find_audio_stream(AVFormatContext *fmtctx)
{
	int i;

	for (i = 0; i < fmtctx->nb_streams; i++)
		if (fmtctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			return i;

	return -1;
}

static int avfmt_read_to_sample(slurp_t *s, AVFormatContext *fmtctx, int astr, song_sample_t *smp)
{
	int success = 0;
	uint32_t flags;
	const AVCodec *codec;
	AVCodecContext *cctx;

	if (!sample_fmt_to_sfflags(fmtctx->streams[astr]->codecpar->format, fmtctx->streams[astr]->codecpar->ch_layout.nb_channels, &flags))
		goto fail;

	codec = schism_avcodec_find_decoder(fmtctx->streams[astr]->codecpar->codec_id);
	if (!codec)
		goto fail; /* ??? */

	cctx = schism_avcodec_alloc_context3(codec);
	if (!cctx)
		goto fail;

	schism_avcodec_parameters_to_context(cctx, fmtctx->streams[astr]->codecpar);
	schism_avcodec_open2(cctx, codec, NULL);

	{
		slurp_t memstream;
		disko_t ds[2] = {0};
		AVPacket *packet; /* friggin packet yo */
		AVFrame *frame;
		uint32_t total_samples = 0;
		int bps = schism_av_get_bytes_per_sample(cctx->sample_fmt);

		if (bps < 0)
			goto fail; /* ??? */

		packet = schism_av_packet_alloc();
		frame = schism_av_frame_alloc();

		/* special case: if we already know the amount of frames,
		 * we can preallocate the space for it. this generally
		 * improves speeds quite a bit since we don't have to keep
		 * reallocating */
		if (fmtctx->streams[astr]->nb_frames > 0) {
			uint64_t bpc = schism_avfmt_get_length_estimate(fmtctx, astr);
			bpc *= bps;

			switch (flags & SF_CHN_MASK) {
			case SF_M:
			case SF_SI: {
				disko_memopen_estimate(&ds[0], bpc * cctx->ch_layout.nb_channels);
				break;
			}
			case SF_SS:
				disko_memopen_estimate(&ds[0], bpc);
				disko_memopen_estimate(&ds[1], bpc);
				break;
			}
		} else {
			/* can't estimate output size; use defaults */
			switch (flags & SF_CHN_MASK) {
			case SF_SS:
				disko_memopen(&ds[1]);
				SCHISM_FALLTHROUGH;
			case SF_SI:
			case SF_M:
				disko_memopen(&ds[0]);
				break;
			}
		}

		for (; schism_av_read_frame(fmtctx, packet) >= 0 && total_samples <= MAX_SAMPLE_LENGTH; schism_av_packet_unref(packet)) {
			int finished = 0;

			if (packet->stream_index != astr)
				continue;

			schism_avcodec_send_packet(cctx, packet);

			while (!schism_avcodec_receive_frame(cctx, frame)) {
				if (total_samples + frame->nb_samples > MAX_SAMPLE_LENGTH) {
					finished = 1;
					break;
				}

				total_samples += frame->nb_samples;

				switch (flags & SF_CHN_MASK) {
				case SF_M:
				case SF_SI:
					/* all data is in frame->data[0] */
					disko_write(&ds[0], frame->data[0], bps * frame->nb_samples * cctx->ch_layout.nb_channels);
					break;
				case SF_SS:
					/* split across multiple buffers */
					disko_write(&ds[0], frame->data[0], bps * frame->nb_samples);
					disko_write(&ds[1], frame->data[1], bps * frame->nb_samples);
					break;
				default:
					/* something is very wrong */
					break;
				}
			}

			if (finished)
				break;
		}

		schism_av_frame_free(&frame);
		schism_av_packet_free(&packet);

		disko_memclose(&ds[0], 1);
		disko_memclose(&ds[1], 1);

		SCHISM_RUNTIME_ASSERT(ds[0].length == ds[1].length || !ds[1].length, "memory streams should have the same length, or the latter should have nothing at all");

		/* okaaay, now read in everything :) */
		if ((flags & SF_CHN_MASK) == SF_SS) {
			slurp_2memstream(&memstream, ds[0].data, ds[1].data, ds[0].length);
		} else {
			slurp_memstream(&memstream, ds[0].data, ds[0].length);
		}

		smp->length = total_samples;
		smp->flags = 0; /* empty */
		smp->c5speed = cctx->sample_rate;

		csf_read_sample(smp, flags, &memstream);

		free(ds[0].data);
		free(ds[1].data);
	}

	success = 1;

fail:
	schism_avcodec_free_context(&cctx);

	return success;
}

static int avfmt_read(slurp_t *s, dmoz_file_t *file, song_sample_t *smp)
{
	int success = 0;
	unsigned char *buffer = NULL;
	AVIOContext *ioctx = NULL;
	AVFormatContext *fmtctx = NULL;
	int astr;

	/* nope */
	if (!avformat_wasinit)
		return 0;

	buffer = schism_av_malloc(SCHISM_AVFORMAT_BUFFER_SIZE);
	if (!buffer)
		goto fail;

	ioctx = schism_avio_alloc_context(buffer, SCHISM_AVFORMAT_BUFFER_SIZE, 0, s, avfmt_read_packet, NULL, avfmt_seek);
	if (!ioctx)
		goto fail;

	fmtctx = schism_avformat_alloc_context();
	if (!fmtctx)
		goto fail;

	fmtctx->pb = ioctx;

	if (schism_avformat_open_input(&fmtctx, (file ? file->path : NULL), NULL, NULL) < 0)
		goto fail;

	/* tracker-ception! :) */
	if (!strcmp(fmtctx->iformat->name, "libopenmpt")
		|| !strcmp(fmtctx->iformat->name, "libmodplug")
		|| !strcmp(fmtctx->iformat->name, "libmikmod")
		|| !strcmp(fmtctx->iformat->name, "flac") /* ignore flac, causes infinite loop */)
		goto fail;

	if (schism_avformat_find_stream_info(fmtctx, NULL) < 0)
		goto fail;

	astr = avfmt_find_audio_stream(fmtctx);
	if (astr < 0)
		goto fail;

	{
		/* metadata */
		const AVDictionary *meta = fmtctx->metadata;
		AVDictionaryEntry *ent;

		ent = schism_av_dict_get(meta, "title", NULL, 0/*AV_DICT_MATCH_CASE*/);
		if (ent) {
			if (file)
				file->title = str_dup(ent->value);
			else if (smp)
				strncpy(smp->name, ent->value, sizeof(smp->name));
		}

		if (file) {
			/* only useful for files */
			ent = schism_av_dict_get(meta, "artist", NULL, 0/*AV_DICT_MATCH_CASE*/);
			if (ent) file->artist = str_dup(ent->value);
		}
	}

	if (file) {
		/* this seems to be in static memory (not allocated);
		 * so I think we're fine just pointing to it, as long as
		 * the library never gets unloaded, which should never
		 * happen anyway  --paper */
		file->description = fmtctx->iformat->long_name ? fmtctx->iformat->long_name : "FFMPEG";

		file->smp_speed = fmtctx->streams[astr]->codecpar->sample_rate;
		file->smp_length = schism_avfmt_get_length_estimate(fmtctx, astr);
	}

	if (smp && !avfmt_read_to_sample(s, fmtctx, astr, smp))
		goto fail;

	success = 1;

fail:
	if (fmtctx) {
		schism_avformat_close_input(&fmtctx);
		schism_avformat_free_context(fmtctx);
	}

	if (ioctx) {
		schism_av_free(ioctx->buffer);
		ioctx->buffer = NULL;
		/* ehhhh */
		schism_avio_context_free(&ioctx);
	} else if (buffer) {
		/* `else if` to avoid double free, since
		 * the ioctx also frees the buffer given to it
		 * (at least, i think it does ...) */
		schism_av_free(buffer);
	}

	return success;

}

int fmt_avformat_read_info(dmoz_file_t *file, slurp_t *s)
{
	if (!avfmt_read(s, file, NULL))
		return 0;

	file->type = TYPE_SAMPLE_COMPR;
	/* TODO: any way to retrieve the title? */

	return 1;
}

int fmt_avformat_load_sample(slurp_t *s, song_sample_t *smp)
{
	if (!avfmt_read(s, NULL, smp))
		return 0;

	return 1;
}

/* ------------------------------------------------- */

static int load_avformat_syms(void);

#ifdef AVFORMAT_DYNAMIC_LOAD

#include "loadso.h"

enum {
	DLIB_AVFORMAT,
	DLIB_AVCODEC,
	DLIB_AVUTIL,

	DLIB_MAX_,
};

static struct {
	const char *name;
	int version;
	void *handle;
} handles[] = {
	[DLIB_AVFORMAT] = {"avformat", LIBAVFORMAT_VERSION_MAJOR, NULL},
	[DLIB_AVCODEC]  = {"avcodec",  LIBAVCODEC_VERSION_MAJOR,  NULL},
	[DLIB_AVUTIL]   = {"avutil",   LIBAVUTIL_VERSION_MAJOR,   NULL},
};

static void avformat_dlend(void)
{
	int i;

	for (i = 0; i < DLIB_MAX_; i++) {
		if (handles[i].handle) {
			loadso_object_unload(handles[i].handle);
			handles[i].handle = NULL;
		}
	}
}

static int avformat_dlinit(void)
{
	int i;

	for (i = 0; i < DLIB_MAX_; i++) {
		if (handles[i].handle)
			continue; /* already have it?? wtf */

		handles[i].handle = library_load(handles[i].name, handles[i].version, 0 /* ? */);
		if (!handles[i].handle)
			return -1;
	}

	int retval = load_avformat_syms();
	if (retval < 0)
		avformat_dlend();

	return retval;
}

// this is always true under SDL but I'm paranoid
SCHISM_STATIC_ASSERT(sizeof(void (*)(void)) == sizeof(void *), "dynamic loading code assumes function pointer and void pointer are of equivalent size");

static int load_avformat_sym(int dlib, const char *fn, void *addr)
{
	void *func = loadso_function_load(handles[dlib].handle, fn);
	if (!func)
		return 0;

	memcpy(addr, &func, sizeof(void *));

	return 1;
}

#define SCHISM_AVFORMAT_SYM(dlib, x) \
	if (!load_avformat_sym(dlib, #x, &schism_##x)) { printf("%s", #x); return -1; }

#else

#define SCHISM_AVFORMAT_SYM(dlib, x) schism_##x = x

static int avformat_dlinit(void)
{
	load_avformat_syms();
	return 0;
}

#endif

static int load_avformat_syms(void)
{
	SCHISM_AVFORMAT_SYM(DLIB_AVUTIL, av_malloc);
	SCHISM_AVFORMAT_SYM(DLIB_AVUTIL, av_free);
	SCHISM_AVFORMAT_SYM(DLIB_AVUTIL, av_log_set_callback);
	SCHISM_AVFORMAT_SYM(DLIB_AVUTIL, av_get_bytes_per_sample);
	SCHISM_AVFORMAT_SYM(DLIB_AVUTIL, av_frame_free);
	SCHISM_AVFORMAT_SYM(DLIB_AVUTIL, av_frame_alloc);
	SCHISM_AVFORMAT_SYM(DLIB_AVUTIL, av_dict_get);

	SCHISM_AVFORMAT_SYM(DLIB_AVFORMAT, avformat_alloc_context);
	SCHISM_AVFORMAT_SYM(DLIB_AVFORMAT, avformat_free_context);
	SCHISM_AVFORMAT_SYM(DLIB_AVFORMAT, avio_alloc_context);
	SCHISM_AVFORMAT_SYM(DLIB_AVFORMAT, avio_context_free);
	SCHISM_AVFORMAT_SYM(DLIB_AVFORMAT, avformat_find_stream_info);
	SCHISM_AVFORMAT_SYM(DLIB_AVFORMAT, avformat_open_input);
	SCHISM_AVFORMAT_SYM(DLIB_AVFORMAT, avformat_close_input);
	SCHISM_AVFORMAT_SYM(DLIB_AVFORMAT, av_read_frame);

	SCHISM_AVFORMAT_SYM(DLIB_AVCODEC, avcodec_free_context);
	SCHISM_AVFORMAT_SYM(DLIB_AVCODEC, avcodec_receive_frame);
	SCHISM_AVFORMAT_SYM(DLIB_AVCODEC, avcodec_send_packet);
	SCHISM_AVFORMAT_SYM(DLIB_AVCODEC, av_packet_unref);
	SCHISM_AVFORMAT_SYM(DLIB_AVCODEC, av_packet_free);
	SCHISM_AVFORMAT_SYM(DLIB_AVCODEC, av_packet_alloc);
	SCHISM_AVFORMAT_SYM(DLIB_AVCODEC, avcodec_open2);
	SCHISM_AVFORMAT_SYM(DLIB_AVCODEC, avcodec_parameters_to_context);
	SCHISM_AVFORMAT_SYM(DLIB_AVCODEC, avcodec_alloc_context3);
	SCHISM_AVFORMAT_SYM(DLIB_AVCODEC, avcodec_find_decoder);

	return 0;
}

/* ------------------------------------------------- */

int avformat_init(void)
{
	if (avformat_dlinit() < 0)
		return 0;

	schism_av_log_set_callback(schism_av_vlog);

	avformat_wasinit = 1;
	return 1;
}

void avformat_quit(void)
{
#ifdef AVFORMAT_DYNAMIC_LOAD
	if (avformat_wasinit)
		avformat_dlend();
#endif
	avformat_wasinit = 0;
}
