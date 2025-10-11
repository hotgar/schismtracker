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

#include "atomic.h"

#if (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_ATOMICS__)

#include <stdatomic.h>

int atm_init(void) { return 0; }
void atm_quit(void) { }

int32_t atm_load(struct atm *atm)
{
	return atomic_load((const _Atomic volatile int32_t *)&atm->x);
}

void atm_store(struct atm *atm, int32_t x)
{
	atomic_store((_Atomic volatile int32_t *)&atm->x, x);
}

void *atm_ptr_load(struct atm_ptr *atm)
{
	return atomic_load((const volatile void * _Atomic*)&atm->x);
}

void atm_ptr_store(struct atm_ptr *atm, void *x)
{
	atomic_store((volatile void *_Atomic *)&atm->x, x);
}

#elif !defined(USE_THREADS)

/* eh */

int atm_init(void) { return 0; }
void atm_quit(void) { }

int32_t atm_load(struct atm *atm)
{
	return atm->x;
}

void atm_store(struct atm *atm, int32_t x)
{
	atm->x = x;
}

void *atm_ptr_load(struct atm_ptr *atm)
{
	return atm->x;
}

void atm_ptr_store(struct atm_ptr *atm, void *x)
{
	atm->x = x;
}

#elif SCHISM_GNUC_HAS_BUILTIN(__atomic_load, 4, 7, 0)

int atm_init(void) { return 0; }
void atm_quit(void) { }

int32_t atm_load(struct atm *atm)
{
	int32_t r;
	__atomic_load(&atm->x, &r, __ATOMIC_SEQ_CST);
	return r;
}

void atm_store(struct atm *atm, int32_t x)
{
	__atomic_store(&atm->x, &x, __ATOMIC_SEQ_CST);
}

void *atm_ptr_load(struct atm_ptr *atm)
{
	void *r;
	__atomic_load(&atm->x, &r, __ATOMIC_SEQ_CST);
	return r;
}

void atm_ptr_store(struct atm_ptr *atm, void *x)
{
	__atomic_store(&atm->x, &x, __ATOMIC_SEQ_CST);
}

#elif SCHISM_GNUC_HAS_BUILTIN(__sync_synchronize, 4, 1, 0)
/* I hope this is right */

int atm_init(void) { return 0; }
void atm_quit(void) { }

int32_t atm_load(struct atm *atm)
{
	__sync_synchronize();
	return atm->x;
}

void atm_store(struct atm *atm, int32_t x)
{
	atm->x = x;
	__sync_synchronize();
}

void *atm_ptr_load(struct atm_ptr *atm)
{
	__sync_synchronize();
	return atm->x;
}

void atm_ptr_store(struct atm_ptr *atm, void *x)
{
	atm->x = x;
	__sync_synchronize();
}

#elif defined(SCHISM_WIN32)
/* Interlocked* */

#include <windows.h>

SCHISM_STATIC_ASSERT(sizeof(LONG) == sizeof(int32_t), "LONG must be 32-bit");

int atm_init(void) { return 0; }
void atm_quit(void) { }

int32_t atm_load(struct atm *atm)
{
	return InterlockedOr((volatile LONG *)&atm->x, 0);
}

void atm_store(struct atm *atm, int32_t x)
{
	InterlockedExchange((volatile LONG *)&atm->x, x);
}

void *atm_ptr_load(struct atm_ptr *atm)
{
#if SIZEOF_VOID_P == 8
	return (void *)InterlockedOr64((volatile LONG64 *)&atm->x, 0);
#elif SIZEOF_VOID_P == 4
	return (void *)InterlockedOr((volatile LONG *)&atm->x, 0);
#else
# error what?
#endif
}

void atm_ptr_store(struct atm_ptr *atm, void *x)
{
#if SIZEOF_VOID_P == 8
	InterlockedExchange64((volatile LONG64 *)&atm->x, (LONG64)x);
#elif SIZEOF_VOID_P == 4
	InterlockedExchange((volatile LONG *)&atm->x, (LONG)x);
#else
# error what?
#endif
}

#else
/* TODO: SDL has atomics, probably with more platforms than
 * we support now. We should be able to import it. */

#include "mt.h"

#define MUTEXES_SIZE (16)

static mt_mutex_t *mutexes[MUTEXES_SIZE] = {0};

int atm_init(void)
{
	uint32_t i;

	for (i = 0; i < MUTEXES_SIZE; i++) {
		mutexes[i] = mt_mutex_create();
		if (!mutexes[i])
			return -1;
	}

	/* at this point, the mutexes array should NEVER be touched again
	 * until we quit. */

	return 0;
}

void atm_quit(void)
{
	uint32_t i;

	for (i = 0; i < MUTEXES_SIZE; i++) {
		if (mutexes[i]) {
			mt_mutex_delete(mutexes[i]);
			mutexes[i] = NULL;
		}
	}
}

/* ------------------------------------------------------------------------ */

static inline SCHISM_ALWAYS_INLINE
mt_mutex_t *atm_get_mutex(struct atm *atm)
{
	/* TODO use alignof() here ... */
	return mutexes[((uintptr_t)atm / sizeof(*atm)) % MUTEXES_SIZE];
}

int atm_load(struct atm *atm)
{
	int r;
	mt_mutex_t *m = atm_get_mutex(atm);

	mt_mutex_lock(m);
	r = atm->x;
	mt_mutex_unlock(m);

	return r;
}

void atm_store(struct atm *atm, int32_t x)
{
	mt_mutex_t *m = atm_get_mutex(atm);

	mt_mutex_lock(m);
	atm->x = x;
	mt_mutex_unlock(m);
}

static inline SCHISM_ALWAYS_INLINE
mt_mutex_t *atm_ptr_get_mutex(struct atm_ptr *atm)
{
	/* TODO use alignof() here ... */
	return mutexes[((uintptr_t)atm / sizeof(*atm)) % MUTEXES_SIZE];
}

void *atm_ptr_load(struct atm_ptr *atm)
{
	void *r;
	mt_mutex_t *m = atm_ptr_get_mutex(atm);

	mt_mutex_lock(m);
	r = atm->x;
	mt_mutex_unlock(m);

	return r;
}

void atm_ptr_store(struct atm_ptr *atm, void *x)
{
	mt_mutex_t *m = atm_ptr_get_mutex(atm);

	mt_mutex_lock(m);
	atm->x = x;
	mt_mutex_unlock(m);
}

#endif
