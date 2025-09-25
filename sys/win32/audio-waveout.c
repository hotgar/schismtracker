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

/* Win32 Waveout audio backend - written because SDL 1.2 kind of sucks, and
 * this driver is especially terrible there.
 *  - paper */

#include "headers.h"
#include "charset.h"
#include "mt.h"
#include "mem.h"
#include "osdefs.h"
#include "backend/audio.h"

#include <windows.h>

#define WAVEHDR_DWUSER_PREPARED (~((DWORD_PTR)0))

/* Define this ourselves; old toolchains don't have it */
struct waveoutcaps2w {
	WORD wMid;
	WORD wPid;
	MMVERSION vDriverVersion;
	WCHAR szPname[MAXPNAMELEN];
	DWORD dwFormats;
	WORD wChannels;
	WORD wReserved1;
	DWORD dwSupport;
	GUID ManufacturerGuid;
	GUID ProductGuid;
	GUID NameGuid;
};

/* This is needed because waveout is weird, and the WAVEHDR buffers need
 * some time to "cool down", so we cycle between buffers. */
#define NUM_BUFFERS 2
SCHISM_STATIC_ASSERT(NUM_BUFFERS >= 2, "NUM_BUFFERS must be at least 2");

struct schism_audio_device {
	struct schism_audio_device_simple simple;

	/* This is for synchronizing the audio thread with
	 * the actual audio device */
	HANDLE sem;

	HWAVEOUT hwaveout;

	/* The allocated raw mixing buffer */
	uint8_t *buffer;

	WAVEHDR wavehdr[NUM_BUFFERS];
	int next_buffer;
};

/* ---------------------------------------------------------- */
/* drivers */

static int waveout_audio_driver_count(void)
{
	return 1;
}

static const char *waveout_audio_driver_name(int i)
{
	switch (i) {
	case 0: return "waveout";
	default: return NULL;
	}
}

/* ------------------------------------------------------------------------ */

/* devices name cache; refreshed after every call to waveout_audio_device_count */
static struct {
	uint32_t id;
	char *name;
} *devices = NULL;
static size_t devices_size = 0;

/* FIXME: This screws up the GUI royally if someone hotplugs a device.
 * The IDs of waveout devices aren't necessarily "unique", so we can't
 * use those; they change any time an audio device is added or removed
 * (annoying!!)
 * The only thing I can think of is opening literally every single
 * device and then calling waveOutGetID() to check if it changed,
 * which is obviously stupid and a waste of resources.
 *
 * NOTE 2025-09-25: can't we just compare the device caps, like we
 * already do for the MIDI stuff? */
static uint32_t waveout_audio_device_count(uint32_t flags)
{
	UINT devs;

	if (flags & AUDIO_BACKEND_CAPTURE)
		return 0;

	devs = waveOutGetNumDevs();

	if (devices) {
		for (size_t i = 0; i < devices_size; i++)
			free(devices[i].name);
		free(devices);
	}

	devices = mem_alloc(sizeof(*devices) * devs);
	devices_size = 0;

	UINT i;
	for (i = 0; i < devs; i++) {
		union {
#ifdef SCHISM_WIN32_COMPILE_ANSI
			WAVEOUTCAPSA a;
#endif
			WAVEOUTCAPSW w;
			struct waveoutcaps2w w2;
		} caps = {0};

		SCHISM_ANSI_UNICODE({
			if (waveOutGetDevCapsA(i, &caps.a, sizeof(caps.a)) != MMSYSERR_NOERROR)
				continue;

			/* Try receiving based on the name GUID. Otherwise, fall back to the short name. */
			if (!win32_audio_lookup_device_name(NULL, &i, &devices[devices_size].name)
				&& charset_iconv(caps.a.szPname, &devices[devices_size].name, CHARSET_ANSI, CHARSET_UTF8, sizeof(caps.a.szPname)))
				continue;
		}, {
			/* Try WAVEOUTCAPS2 before WAVEOUTCAPS */
			if (waveOutGetDevCapsW(i, (LPWAVEOUTCAPSW)&caps.w2, sizeof(caps.w2)) == MMSYSERR_NOERROR) {
				/* Try receiving based on the name GUID. Otherwise, fall back to the short name. */
				if (!win32_audio_lookup_device_name(&caps.w2.NameGuid, &i, &devices[devices_size].name)
					&& charset_iconv(caps.w2.szPname, &devices[devices_size].name, CHARSET_WCHAR_T, CHARSET_UTF8, sizeof(caps.w2.szPname)))
					continue;
			} else if (waveOutGetDevCapsW(i, &caps.w, sizeof(caps.w)) == MMSYSERR_NOERROR) {
				if (!win32_audio_lookup_device_name(NULL, &i, &devices[devices_size].name)
					&& charset_iconv(caps.w.szPname, &devices[devices_size].name, CHARSET_WCHAR_T, CHARSET_UTF8, sizeof(caps.w.szPname)))
					continue;
			} else {
				continue;
			}
		})

		devices[devices_size].id = i;

		devices_size++;
	}

	return devs;
}

static const char *waveout_audio_device_name(uint32_t i)
{
	/* If this ever happens it is a catastrophic bug and we
	 * should crash before anything bad happens. */
	if (i >= devices_size)
		return NULL;

	return devices[i].name;
}

/* ---------------------------------------------------------- */

static int waveout_audio_init_driver(const char *driver)
{
	if (strcmp(driver, "waveout"))
		return -1;

	/* Get the devices */
	(void)waveout_audio_device_count(0);
	return 0;
}

static void waveout_audio_quit_driver(void)
{
	/* Free the devices */
	if (devices) {
		for (size_t i = 0; i < devices_size; i++)
			free(devices[i].name);
		free(devices);

		devices = NULL;
	}
}

/* ------------------------------------------------------------------------ */

static void *waveout_get_buffer(schism_audio_device_t *dev, size_t *buflen)
{
	*buflen = dev->wavehdr[dev->next_buffer].dwBufferLength;
	return dev->wavehdr[dev->next_buffer].lpData;
}

static int waveout_play(schism_audio_device_t *dev)
{
	waveOutWrite(dev->hwaveout, &dev->wavehdr[dev->next_buffer], sizeof(dev->wavehdr[dev->next_buffer]));

	return 0;
}

static int waveout_wait(schism_audio_device_t *dev)
{
	dev->next_buffer = (dev->next_buffer + 1) % NUM_BUFFERS;

	/* wait infinitely. when we're closed, it will send a signal here,
	 * and the device will be cancelled. */
	WaitForSingleObject(dev->sem, INFINITE);

	return 0;
}

static void waveout_aftercancel(schism_audio_device_t *dev)
{
	/* Release the semaphore, to wake up the waiting thread.
	 * Prevents deadlocks */
	ReleaseSemaphore(dev->sem, 1, NULL);
}

static const struct schism_audio_device_simple_vtable waveout_vtbl = {
	waveout_get_buffer,
	waveout_play,
	waveout_wait,
	waveout_aftercancel,
};

/* ------------------------------------------------------------------------ */

static void CALLBACK waveout_audio_callback(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD dwParam1, DWORD dwParam2)
{
	schism_audio_device_t *dev = (schism_audio_device_t *)dwInstance;

	/* don't care about other messages */
	switch (uMsg) {
	case WOM_DONE:
		ReleaseSemaphore(dev->sem, 1, NULL);
	default:
		return;
	}
}

/* decl */
static void waveout_audio_close_device(schism_audio_device_t *dev);

/* nonzero on success */
static schism_audio_device_t *waveout_audio_open_device(uint32_t id, const schism_audio_spec_t *desired, schism_audio_spec_t *obtained)
{
	/* Default to some device that can handle our output */
	UINT device_id = (id == AUDIO_BACKEND_DEFAULT || id < devices_size) ? (WAVE_MAPPER) : devices[id].id;

	/* Fill in the format structure */
	WAVEFORMATEX format = {
		.wFormatTag = WAVE_FORMAT_PCM,
		.nChannels = desired->channels,
		.nSamplesPerSec = desired->freq,
	};

	/* filter invalid bps values (should never happen, but eh...) */
	switch (desired->bits) {
	case 8: format.wBitsPerSample = 8; break;
	default:
	case 16: format.wBitsPerSample = 16; break;
	case 32: format.wBitsPerSample = 32; break;
	}

	/* ok, now we can allocate the device */
	schism_audio_device_t *dev = mem_calloc(1, sizeof(*dev));

	for (;;) {
		/* Recalculate format */
		format.nBlockAlign = format.nChannels * (format.wBitsPerSample / 8);
		format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

		MMRESULT err = waveOutOpen(&dev->hwaveout, device_id, &format, (UINT_PTR)waveout_audio_callback, (UINT_PTR)dev, CALLBACK_FUNCTION | WAVE_ALLOWSYNC);
		if (err == MMSYSERR_NOERROR) {
			/* We're done here */
			break;
		} else if (err == WAVERR_BADFORMAT) {
			/* Retry with 16-bit. 32-bit samples don't work everywhere
			 * (notably windows xp is broken and so is everything before it) */
			if (format.wBitsPerSample == 32) {
				format.wBitsPerSample = 16;
				continue;
			}
		}

		/* Punt if we failed and we can't do anything about it */
		goto fail;
	}

	dev->sem = CreateSemaphoreA(NULL, 1, NUM_BUFFERS, "WinMM audio sync semaphore");
	if (!dev->sem)
		goto fail;

	/* allocate the buffer */
	DWORD buflen = desired->samples * format.nChannels * (format.wBitsPerSample / 8);
	dev->buffer = mem_alloc(buflen * NUM_BUFFERS);

	/* fill in the wavehdrs */
	for (int i = 0; i < NUM_BUFFERS; i++) {
		dev->wavehdr[i].lpData = (LPSTR)dev->buffer + (buflen * i);
		dev->wavehdr[i].dwBufferLength = buflen;
		dev->wavehdr[i].dwFlags = WHDR_DONE;

		if (waveOutPrepareHeader(dev->hwaveout, &dev->wavehdr[i], sizeof(dev->wavehdr[i])) != MMSYSERR_NOERROR)
			goto fail;

		dev->wavehdr[i].dwUser = WAVEHDR_DWUSER_PREPARED;
	}

	if (audio_simple_init(dev, &waveout_vtbl, desired->callback))
		goto fail;

	obtained->freq = format.nSamplesPerSec;
	obtained->channels = format.nChannels;
	obtained->bits = format.wBitsPerSample;
	obtained->samples = desired->samples;

	return dev;

fail:
	waveout_audio_close_device(dev);

	return NULL;
}

static void waveout_audio_close_device(schism_audio_device_t *dev)
{
	int i;

	if (!dev)
		return;

	audio_simple_close(&dev->simple);

	/* kill the output */
	if (dev->hwaveout)
		waveOutClose(dev->hwaveout);

	/* "unprepare" all of our buffers */
	for (i = 0; i < NUM_BUFFERS; i++) {
		if (dev->wavehdr[i].dwUser != WAVEHDR_DWUSER_PREPARED)
			continue;

		/* sleep until the device is done with our buffer */
		while (!(dev->wavehdr[i].dwFlags & WHDR_DONE))
			timer_delay(10);

		waveOutUnprepareHeader(dev->hwaveout, &dev->wavehdr[i], sizeof(dev->wavehdr[i]));
	}

	if (dev->buffer)
		free(dev->buffer);

	if (dev->sem)
		CloseHandle(dev->sem);

	free(dev);
}

/* -------------------------------------------------------------------- */
/* dynamic loading */

static int waveout_audio_init(void)
{
	return 1;
}

static void waveout_audio_quit(void)
{
	/* dont do anything */
}

/* -------------------------------------------------------------------- */

const schism_audio_backend_t schism_audio_backend_waveout = {
	.init = waveout_audio_init,
	.quit = waveout_audio_quit,

	.driver_count = waveout_audio_driver_count,
	.driver_name = waveout_audio_driver_name,

	.device_count = waveout_audio_device_count,
	.device_name = waveout_audio_device_name,

	.init_driver = waveout_audio_init_driver,
	.quit_driver = waveout_audio_quit_driver,

	.open_device = waveout_audio_open_device,
	.close_device = waveout_audio_close_device,
	.lock_device = audio_simple_device_lock,
	.unlock_device = audio_simple_device_unlock,
	.pause_device = audio_simple_device_pause,
};
