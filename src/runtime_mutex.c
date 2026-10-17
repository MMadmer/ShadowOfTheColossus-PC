/* Opaque PS4 mutex/condition variable handles: pthread-backed on Linux, SRW locks on Windows. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
typedef struct { int type; } GuestAttr;
#ifdef _WIN32
#include <windows.h>
static unsigned long current_tid(void) { return GetCurrentThreadId(); }
/* sotc: an SRW lock held once; owner and recursion depth are tracked here. */
typedef struct { SRWLOCK lock; unsigned long owner; unsigned depth; int type; } GuestMutex;
#else
#include <unistd.h>
static unsigned long current_tid(void) { return (unsigned long)gettid(); }
/* sotc: owner and lock site are kept for hang reports (long waits are reported). */
typedef struct { pthread_mutex_t native; unsigned long owner; void *site; unsigned depth; } GuestMutex;
#endif
#ifdef _WIN32
static size_t created;
#else
static size_t created, locks, unlocks;
#endif
static int32_t orbis_error(int e) {
    if (!e) return 0;
    unsigned code;
    switch (e) {
    case EPERM: code=1; break;
    case ENOMEM: code=12; break;
    case EBUSY: code=16; break;
    case EINVAL: code=22; break;
    case EAGAIN: code=35; break;
    case EDEADLK: code=11; break;
    default: fprintf(stderr, "STOP: unmapped host pthread error %d\n", e); exit(21);
    }
    return (int32_t)(UINT32_C(0x80020000) | code);
}
static ABI int32_t attr_init(GuestAttr **out) {
    if (!out) return orbis_error(EINVAL);
    GuestAttr *attr = malloc(sizeof(*attr));
    if (!attr) return orbis_error(ENOMEM);
    attr->type = 1; *out = attr; return 0;
}
static ABI int32_t attr_type(GuestAttr **attr, int type) {
    if (!attr || !*attr || type < 1 || type > 4) return orbis_error(EINVAL);
    (*attr)->type = type; return 0;
}
/* Priority protocols need a PS4 scheduler; the value is validated and recorded only. */
static ABI int32_t attr_protocol(GuestAttr **attr, int protocol) {
    if (!attr || !*attr || protocol < 0 || protocol > 2) return orbis_error(EINVAL);
    return 0;
}
static ABI int32_t attr_destroy(GuestAttr **attr) {
    if (!attr || !*attr) return orbis_error(EINVAL);
    free(*attr); *attr = NULL; return 0;
}
#ifndef _WIN32
static ABI int32_t mutex_init(GuestMutex **out, GuestAttr **attr, const char *name) {
    (void)name;
    if (!out || (attr && !*attr)) return orbis_error(EINVAL);
    int type = attr ? (*attr)->type : 1;
    if (type < 1 || type > 4) return orbis_error(EINVAL);
    GuestMutex *mutex = calloc(1, sizeof(*mutex));
    if (!mutex) return orbis_error(ENOMEM);
    pthread_mutexattr_t native_attr;
    int e = pthread_mutexattr_init(&native_attr);
    if (e) { free(mutex); return orbis_error(e); }
    int native_type = type == 2 ? PTHREAD_MUTEX_RECURSIVE : type == 3 ? PTHREAD_MUTEX_NORMAL : PTHREAD_MUTEX_ERRORCHECK;
    e = pthread_mutexattr_settype(&native_attr, native_type);
    if (!e) e = pthread_mutex_init(&mutex->native, &native_attr);
    pthread_mutexattr_destroy(&native_attr);
    if (e) { free(mutex); return orbis_error(e); }
    *out = mutex; ++created; return 0;
}
static pthread_mutex_t static_init = PTHREAD_MUTEX_INITIALIZER;
/* Statically initialized handles (0/1) are created once, even under contention. */
static int32_t ensure_mutex(GuestMutex **mutex) {
    if (!mutex) return orbis_error(EINVAL);
    uintptr_t value = __atomic_load_n((uintptr_t *)mutex, __ATOMIC_ACQUIRE);
    if (value == 2) return orbis_error(EINVAL);
    if (value >= 2) return 0;
    pthread_mutex_lock(&static_init);
    int32_t e = 0;
    if ((uintptr_t)*mutex < 2) {
        GuestMutex *created_mutex = NULL;
        e = mutex_init(&created_mutex, NULL, NULL);
        if (!e) __atomic_store_n(mutex, created_mutex, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&static_init);
    return e;
}
static ABI int32_t mutex_lock(GuestMutex **mutex) {
    int32_t e = ensure_mutex(mutex);
    if (e) return e;
    GuestMutex *m = *mutex;
    int native = pthread_mutex_trylock(&m->native);
    for (int waited = 0; native == EBUSY;) {
        struct timespec slice;
        clock_gettime(CLOCK_REALTIME, &slice);
        slice.tv_sec += 5;
        native = pthread_mutex_timedlock(&m->native, &slice);
        if (native == ETIMEDOUT) {
            native = EBUSY;
            if (waited++ < 3)
                fprintf(stderr, "Runtime: mutex %p waited %ds by thread %lu from %p; owner thread %lu locked from %p (depth %u)\n",
                        (void *)m, waited * 5, current_tid(), __builtin_return_address(0), m->owner, m->site, m->depth);
        }
    }
    e = orbis_error(native);
    if (!e) { ++locks; m->owner = current_tid(); m->site = __builtin_return_address(0); ++m->depth; }
    return e;
}
static ABI int32_t mutex_trylock(GuestMutex **mutex) {
    int32_t e = ensure_mutex(mutex);
    if (e) return e;
    e = orbis_error(pthread_mutex_trylock(&(*mutex)->native));
    if (!e) ++locks;
    return e;
}
static ABI int32_t mutex_unlock(GuestMutex **mutex) {
    if (!mutex || (uintptr_t)*mutex == 2) return orbis_error(EINVAL);
    if ((uintptr_t)*mutex < 2) return orbis_error(EPERM);
    GuestMutex *m = *mutex;
    unsigned depth = m->depth;
    if (depth) --m->depth;
    if (!m->depth) m->owner = 0;
    int32_t e = orbis_error(pthread_mutex_unlock(&m->native));
    if (e) m->depth = depth;
    if (!e) ++unlocks;
    else {
        static _Atomic unsigned failures;
        if (failures++ < 8)
            fprintf(stderr, "Runtime: mutex %p unlock failed 0x%x from %p\n", (void *)*mutex, (unsigned)e,
                    __builtin_return_address(0));
    }
    return e;
}
static ABI int32_t mutex_destroy(GuestMutex **mutex) {
    if (!mutex || (uintptr_t)*mutex == 2) return orbis_error(EINVAL);
    if ((uintptr_t)*mutex < 2) return 0;
    int e = pthread_mutex_destroy(&(*mutex)->native);
    if (!e) { free(*mutex); *mutex = (GuestMutex *)(uintptr_t)2; }
    return orbis_error(e);
}
static int deadline_after(struct timespec *end, uint64_t usec) {
    if (clock_gettime(CLOCK_REALTIME, end)) return errno;
    uint64_t ns = (uint64_t)end->tv_nsec + (usec % 1000000) * 1000;
    end->tv_sec += (time_t)(usec / 1000000 + ns / 1000000000);
    end->tv_nsec = (long)(ns % 1000000000);
    return 0;
}
static int32_t timed_error(int e) { return e == ETIMEDOUT ? (int32_t)UINT32_C(0x8002003c) : orbis_error(e); }
static ABI int32_t mutex_timedlock(GuestMutex **mutex, uint32_t usec) {
    int32_t e = ensure_mutex(mutex);
    if (e) return e;
    struct timespec end;
    if (deadline_after(&end, usec)) return orbis_error(EINVAL);
    e = timed_error(pthread_mutex_timedlock(&(*mutex)->native, &end));
    if (!e) ++locks;
    return e;
}
/* Condition variables: opaque handles over host pthread_cond (CLOCK_REALTIME,
 * like FreeBSD's default), sharing the host mutex behind GuestMutex. */
typedef struct { pthread_cond_t native; } GuestCond;
static size_t conds, waits, wakeups;
static ABI int32_t cond_init(GuestCond **out, void **attr, const char *name) {
    (void)attr; (void)name;
    if (!out) return orbis_error(EINVAL);
    GuestCond *c = malloc(sizeof(*c));
    if (!c) return orbis_error(ENOMEM);
    int e = pthread_cond_init(&c->native, NULL);
    if (e) { free(c); return orbis_error(e); }
    *out = c; ++conds; return 0;
}
static int32_t ensure_cond(GuestCond **cond) {
    if (!cond) return orbis_error(EINVAL);
    if (__atomic_load_n((uintptr_t *)cond, __ATOMIC_ACQUIRE) >= 2) return 0;
    pthread_mutex_lock(&static_init);
    int32_t e = 0;
    if ((uintptr_t)*cond < 2) {
        GuestCond *c = NULL;
        e = cond_init(&c, NULL, NULL);
        if (!e) __atomic_store_n(cond, c, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&static_init);
    return e;
}
static ABI int32_t cond_destroy(GuestCond **cond) {
    if (!cond) return orbis_error(EINVAL);
    if ((uintptr_t)*cond < 2) return 0;
    int e = pthread_cond_destroy(&(*cond)->native);
    if (!e) { free(*cond); *cond = NULL; }
    return orbis_error(e);
}
static ABI int32_t cond_wait(GuestCond **cond, GuestMutex **mutex) {
    int32_t e = ensure_cond(cond);
    if (e) return e;
    if (!mutex || (uintptr_t)*mutex < 3) return orbis_error(EINVAL);
    ++waits;
    return orbis_error(pthread_cond_wait(&(*cond)->native, &(*mutex)->native));
}
static int32_t cond_wait_until(GuestCond **cond, GuestMutex **mutex, const struct timespec *end) {
    int32_t e = ensure_cond(cond);
    if (e) return e;
    if (!mutex || (uintptr_t)*mutex < 3 || !end) return orbis_error(EINVAL);
    ++waits;
    return timed_error(pthread_cond_timedwait(&(*cond)->native, &(*mutex)->native, end));
}
static ABI int32_t cond_timedwait(GuestCond **cond, GuestMutex **mutex, uint32_t usec) {
    struct timespec end;
    if (deadline_after(&end, usec)) return orbis_error(EINVAL);
    return cond_wait_until(cond, mutex, &end);
}
static ABI int32_t cond_signal(GuestCond **cond) {
    int32_t e = ensure_cond(cond);
    if (e) return e;
    ++wakeups; return orbis_error(pthread_cond_signal(&(*cond)->native));
}
static ABI int32_t cond_broadcast(GuestCond **cond) {
    int32_t e = ensure_cond(cond);
    if (e) return e;
    ++wakeups; return orbis_error(pthread_cond_broadcast(&(*cond)->native));
}
#endif
#ifdef _WIN32
/* sotc: Windows: SRW locks and condition variables. winpthreads' timed mutex locks (used for every
 * contended lock to report hangs) and its condition variables went through kernel objects on every
 * contended lock and wake-up: the game's four job workers fighting over their queue spent most of
 * their time in system calls. The guest semantics are kept: error-checking (PS4 default),
 * recursive, normal and adaptive types; the owner and depth are tracked here, the SRW lock is held
 * once. */
static ABI int32_t mutex_init(GuestMutex **out, GuestAttr **attr, const char *name) {
    (void)name;
    if (!out || (attr && !*attr)) return orbis_error(EINVAL);
    int type = attr ? (*attr)->type : 1;
    if (type < 1 || type > 4) return orbis_error(EINVAL);
    GuestMutex *mutex = calloc(1, sizeof(*mutex));
    if (!mutex) return orbis_error(ENOMEM);
    InitializeSRWLock(&mutex->lock);
    mutex->type = type;
    *out = mutex;
    __atomic_add_fetch(&created, 1, __ATOMIC_RELAXED);
    return 0;
}
static SRWLOCK static_init = SRWLOCK_INIT;
/* Statically initialized handles (0/1) are created once, even under contention. */
static int32_t ensure_mutex(GuestMutex **mutex) {
    if (!mutex) return orbis_error(EINVAL);
    uintptr_t value = __atomic_load_n((uintptr_t *)mutex, __ATOMIC_ACQUIRE);
    if (value == 2) return orbis_error(EINVAL);
    if (value >= 2) return 0;
    AcquireSRWLockExclusive(&static_init);
    int32_t e = 0;
    if ((uintptr_t)*mutex < 2) {
        GuestMutex *created_mutex = NULL;
        e = mutex_init(&created_mutex, NULL, NULL);
        if (!e) __atomic_store_n(mutex, created_mutex, __ATOMIC_RELEASE);
    }
    ReleaseSRWLockExclusive(&static_init);
    return e;
}
/* SOTC_MUTEX_PROF=1: contended locks counted per guest call site, the top ones printed every 10 s
 * (absolute addresses; the guest image base is in the "Image at" line). */
typedef struct { uintptr_t site; GuestMutex *mutex; long count; } ContendedSite;
static ContendedSite contended[512];
static int mutex_prof = -1;
static void note_contended(uintptr_t site, GuestMutex *m) {
    size_t i = (size_t)((site * UINT64_C(0x9E3779B97F4A7C15)) >> 55);
    for (size_t n = 0; n < 512; ++n, i = (i + 1) & 511) {
        uintptr_t seen = __atomic_load_n(&contended[i].site, __ATOMIC_RELAXED);
        if (!seen && __atomic_compare_exchange_n(&contended[i].site, &seen, site, 0, __ATOMIC_RELAXED,
                                                 __ATOMIC_RELAXED)) {
            contended[i].mutex = m;
            seen = site;
        }
        if (seen == site) {
            __atomic_add_fetch(&contended[i].count, 1, __ATOMIC_RELAXED);
            break;
        }
    }
    static uint64_t last;
    uint64_t now = GetTickCount64(), before = __atomic_load_n(&last, __ATOMIC_RELAXED);
    if (!before) { __atomic_store_n(&last, now, __ATOMIC_RELAXED); return; }
    if (now - before < 10000 || !__atomic_compare_exchange_n(&last, &before, now, 0, __ATOMIC_RELAXED,
                                                              __ATOMIC_RELAXED))
        return;
    char line[1024];
    int len = snprintf(line, sizeof(line), "SOTC mutex contention (10 s):");
    for (int k = 0; k < 10; ++k) {
        size_t best = 512;
        for (size_t j = 0; j < 512; ++j)
            if (contended[j].count > 0 && (best == 512 || contended[j].count > contended[best].count)) best = j;
        if (best == 512) break;
        len += snprintf(line + len, sizeof(line) - (size_t)len, " %p/%p:%ld", (void *)contended[best].site,
                        (void *)contended[best].mutex, contended[best].count);
        contended[best].count = -contended[best].count;
    }
    for (size_t j = 0; j < 512; ++j) contended[j].count = 0;
    fprintf(stderr, "%s\n", line);
}
static void mutex_take(GuestMutex *m, unsigned long me, uintptr_t site) {
    if (!TryAcquireSRWLockExclusive(&m->lock)) {
        if (mutex_prof < 0) {
            const char *v = getenv("SOTC_MUTEX_PROF");
            mutex_prof = v && v[0] == '1';
        }
        if (mutex_prof) note_contended(site, m);
        /* No spin of its own: the SRW lock spins briefly before it sleeps, and on four cores a
         * spinning job worker only takes the time of the thread holding the lock. */
        AcquireSRWLockExclusive(&m->lock);
    }
    __atomic_store_n(&m->owner, me, __ATOMIC_RELAXED);
    m->depth = 1;
}
static ABI int32_t mutex_lock(GuestMutex **mutex) {
    int32_t e = ensure_mutex(mutex);
    if (e) return e;
    GuestMutex *m = *mutex;
    const unsigned long me = current_tid();
    if (__atomic_load_n(&m->owner, __ATOMIC_RELAXED) == me) {
        if (m->type == 2) { ++m->depth; return 0; }
        if (m->type != 3) return orbis_error(EDEADLK);
    }
    mutex_take(m, me, (uintptr_t)__builtin_return_address(0));
    return 0;
}
static ABI int32_t mutex_trylock(GuestMutex **mutex) {
    int32_t e = ensure_mutex(mutex);
    if (e) return e;
    GuestMutex *m = *mutex;
    const unsigned long me = current_tid();
    if (__atomic_load_n(&m->owner, __ATOMIC_RELAXED) == me) {
        if (m->type == 2) { ++m->depth; return 0; }
        return orbis_error(EBUSY);
    }
    if (!TryAcquireSRWLockExclusive(&m->lock)) return orbis_error(EBUSY);
    __atomic_store_n(&m->owner, me, __ATOMIC_RELAXED);
    m->depth = 1;
    return 0;
}
/* sotc: SotC's job workers (bpe::threading::JobSystemScheduler::DoJobScheduling, EU 1.01) poll
 * the scheduler lock without pause while a job waits on a counter: on PS4 each has a core of
 * its own. A worker whose polls keep coming back empty (the unlock of the no-job path) naps
 * between them; a job (the unlock of the job path) ends the streak. On a 4-core PC the polling
 * took the cores, and a laptop's turbo budget, of the renderer and the game thread.
 * SOTC_POLL_NAP_US (default 100, 0: off), SOTC_POLL_STREAK (default 64 empty polls). */
static uintptr_t poll_empty_site, poll_job_site;
static uint64_t poll_nap_ns;
static unsigned poll_streak_limit;
static int poll_ready;
static __thread unsigned poll_streak;
static uintptr_t call_target(uintptr_t call) {
    int32_t rel;
    memcpy(&rel, (const void *)(call + 1), sizeof(rel));
    return call + 5 + (uintptr_t)(intptr_t)rel;
}
static void poll_setup(void) {
    const char *v = getenv("SOTC_POLL_NAP_US");
    poll_nap_ns = (v ? strtoull(v, NULL, 10) : 100) * 1000;
    v = getenv("SOTC_POLL_STREAK");
    poll_streak_limit = v ? (unsigned)strtoul(v, NULL, 10) : 64;
    const uintptr_t base = runtime_guest_image_base;
    /* Both sites must still be calls of the scheduler's unlock thunk. */
    const uintptr_t empty_call = base + 0x38af3, job_call = base + 0x38a60, unlock = base + 0x3b3c0;
    if (base && poll_nap_ns && *(const uint8_t *)empty_call == 0xe8 && *(const uint8_t *)job_call == 0xe8 &&
        call_target(empty_call) == unlock && call_target(job_call) == unlock) {
        poll_empty_site = empty_call + 5;
        poll_job_site = job_call + 5;
    }
    __atomic_store_n(&poll_ready, 1, __ATOMIC_RELEASE);
}
static ABI int32_t mutex_unlock(GuestMutex **mutex) {
    if (!mutex || (uintptr_t)*mutex == 2) return orbis_error(EINVAL);
    if ((uintptr_t)*mutex < 2) return orbis_error(EPERM);
    GuestMutex *m = *mutex;
    if (__atomic_load_n(&m->owner, __ATOMIC_RELAXED) != current_tid()) {
        static unsigned failures;
        if (__atomic_fetch_add(&failures, 1, __ATOMIC_RELAXED) < 8)
            fprintf(stderr, "Runtime: mutex %p unlock by a thread that does not own it, from %p\n",
                    (void *)m, __builtin_return_address(0));
        return orbis_error(EPERM);
    }
    if (--m->depth) return 0;
    __atomic_store_n(&m->owner, 0, __ATOMIC_RELAXED);
    ReleaseSRWLockExclusive(&m->lock);
    if (!__atomic_load_n(&poll_ready, __ATOMIC_ACQUIRE)) poll_setup();
    const uintptr_t site = (uintptr_t)__builtin_return_address(0);
    if (site == poll_empty_site) {
        if (++poll_streak >= poll_streak_limit) compat_sleep_ns(poll_nap_ns);
    } else if (site == poll_job_site) {
        poll_streak = 0;
    }
    return 0;
}
static ABI int32_t mutex_destroy(GuestMutex **mutex) {
    if (!mutex || (uintptr_t)*mutex == 2) return orbis_error(EINVAL);
    if ((uintptr_t)*mutex < 2) return 0;
    GuestMutex *m = *mutex;
    if (__atomic_load_n(&m->owner, __ATOMIC_RELAXED) != 0) return orbis_error(EBUSY);
    free(m);
    *mutex = (GuestMutex *)(uintptr_t)2;
    return 0;
}
static int deadline_after(struct timespec *end, uint64_t usec) {
    if (clock_gettime(CLOCK_REALTIME, end)) return errno;
    uint64_t ns = (uint64_t)end->tv_nsec + (usec % 1000000) * 1000;
    end->tv_sec += (time_t)(usec / 1000000 + ns / 1000000000);
    end->tv_nsec = (long)(ns % 1000000000);
    return 0;
}
/* Milliseconds left until the CLOCK_REALTIME deadline (rounded up), 0 when it has passed. */
static DWORD ms_until(const struct timespec *end) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    const int64_t ns = ((int64_t)end->tv_sec - (int64_t)now.tv_sec) * 1000000000 +
                       ((int64_t)end->tv_nsec - (int64_t)now.tv_nsec);
    if (ns <= 0) return 0;
    const int64_t ms = (ns + 999999) / 1000000;
    return ms >= (int64_t)INFINITE ? INFINITE - 1 : (DWORD)ms;
}
static int32_t timed_error(int e) { return e == ETIMEDOUT ? (int32_t)UINT32_C(0x8002003c) : orbis_error(e); }
static ABI int32_t mutex_timedlock(GuestMutex **mutex, uint32_t usec) {
    int32_t e = ensure_mutex(mutex);
    if (e) return e;
    GuestMutex *m = *mutex;
    const unsigned long me = current_tid();
    if (__atomic_load_n(&m->owner, __ATOMIC_RELAXED) == me) {
        if (m->type == 2) { ++m->depth; return 0; }
        if (m->type != 3) return orbis_error(EDEADLK);
    }
    struct timespec end;
    if (deadline_after(&end, usec)) return orbis_error(EINVAL);
    for (unsigned spin = 0; !TryAcquireSRWLockExclusive(&m->lock); ++spin) {
        if (!ms_until(&end)) return timed_error(ETIMEDOUT);
        if (spin < 64) YieldProcessor();
        else Sleep(spin < 128 ? 0 : 1);
    }
    __atomic_store_n(&m->owner, me, __ATOMIC_RELAXED);
    m->depth = 1;
    return 0;
}
/* Condition variables over the same SRW lock. */
typedef struct { CONDITION_VARIABLE native; } GuestCond;
static size_t conds;
static ABI int32_t cond_init(GuestCond **out, void **attr, const char *name) {
    (void)attr; (void)name;
    if (!out) return orbis_error(EINVAL);
    GuestCond *c = malloc(sizeof(*c));
    if (!c) return orbis_error(ENOMEM);
    InitializeConditionVariable(&c->native);
    *out = c;
    __atomic_add_fetch(&conds, 1, __ATOMIC_RELAXED);
    return 0;
}
static int32_t ensure_cond(GuestCond **cond) {
    if (!cond) return orbis_error(EINVAL);
    if (__atomic_load_n((uintptr_t *)cond, __ATOMIC_ACQUIRE) >= 2) return 0;
    AcquireSRWLockExclusive(&static_init);
    int32_t e = 0;
    if ((uintptr_t)*cond < 2) {
        GuestCond *c = NULL;
        e = cond_init(&c, NULL, NULL);
        if (!e) __atomic_store_n(cond, c, __ATOMIC_RELEASE);
    }
    ReleaseSRWLockExclusive(&static_init);
    return e;
}
static ABI int32_t cond_destroy(GuestCond **cond) {
    if (!cond) return orbis_error(EINVAL);
    if ((uintptr_t)*cond < 2) return 0;
    free(*cond);
    *cond = NULL;
    return 0;
}
/* Waits with the mutex held once by this thread (a recursive one deeper is restored after). */
static int32_t cond_sleep(GuestCond **cond, GuestMutex **mutex, DWORD ms) {
    int32_t e = ensure_cond(cond);
    if (e) return e;
    if (!mutex || (uintptr_t)*mutex < 3) return orbis_error(EINVAL);
    GuestMutex *m = *mutex;
    const unsigned long me = current_tid();
    if (__atomic_load_n(&m->owner, __ATOMIC_RELAXED) != me) return orbis_error(EPERM);
    const unsigned depth = m->depth;
    __atomic_store_n(&m->owner, 0, __ATOMIC_RELAXED);
    m->depth = 0;
    const BOOL woken = SleepConditionVariableSRW(&(*cond)->native, &m->lock, ms, 0);
    const DWORD error = woken ? 0 : GetLastError();
    __atomic_store_n(&m->owner, me, __ATOMIC_RELAXED);
    m->depth = depth;
    if (!woken) return error == ERROR_TIMEOUT ? timed_error(ETIMEDOUT) : orbis_error(EINVAL);
    return 0;
}
static ABI int32_t cond_wait(GuestCond **cond, GuestMutex **mutex) {
    return cond_sleep(cond, mutex, INFINITE);
}
static int32_t cond_wait_until(GuestCond **cond, GuestMutex **mutex, const struct timespec *end) {
    if (!end) return orbis_error(EINVAL);
    return cond_sleep(cond, mutex, ms_until(end));
}
static ABI int32_t cond_timedwait(GuestCond **cond, GuestMutex **mutex, uint32_t usec) {
    struct timespec end;
    if (deadline_after(&end, usec)) return orbis_error(EINVAL);
    return cond_wait_until(cond, mutex, &end);
}
static ABI int32_t cond_signal(GuestCond **cond) {
    int32_t e = ensure_cond(cond);
    if (e) return e;
    WakeConditionVariable(&(*cond)->native);
    return 0;
}
static ABI int32_t cond_broadcast(GuestCond **cond) {
    int32_t e = ensure_cond(cond);
    if (e) return e;
    WakeAllConditionVariable(&(*cond)->native);
    return 0;
}
#endif
/* PS4 struct timespec is {int64 sec, int64 nsec}, identical to Linux x86-64. */
typedef struct { int64_t sec, nsec; } GuestTimespec;
static ABI int32_t cond_abs_timedwait(GuestCond **cond, GuestMutex **mutex, const GuestTimespec *abs) {
    if (!abs || abs->nsec < 0 || abs->nsec >= 1000000000) return orbis_error(EINVAL);
    struct timespec end = {.tv_sec = (time_t)abs->sec, .tv_nsec = (long)abs->nsec};
    return cond_wait_until(cond, mutex, &end);
}
/* POSIX pthread APIs return positive guest errno values, unlike scePthread. */
static int32_t posix_result(int32_t result) { return result ? (int32_t)((uint32_t)result & 0xffff) : 0; }
static ABI int32_t posix_attr_init(GuestAttr **out) { return posix_result(attr_init(out)); }
static ABI int32_t posix_attr_type(GuestAttr **attr,int type) { return posix_result(attr_type(attr,type)); }
static ABI int32_t posix_attr_destroy(GuestAttr **attr) { return posix_result(attr_destroy(attr)); }
static ABI int32_t posix_mutex_init(GuestMutex **out,GuestAttr **attr) { return posix_result(mutex_init(out,attr,NULL)); }
static ABI int32_t posix_mutex_destroy(GuestMutex **m) { return posix_result(mutex_destroy(m)); }
static ABI int32_t posix_mutex_lock(GuestMutex **m) { return posix_result(mutex_lock(m)); }
static ABI int32_t posix_mutex_trylock(GuestMutex **m) { return posix_result(mutex_trylock(m)); }
static ABI int32_t posix_mutex_unlock(GuestMutex **m) { return posix_result(mutex_unlock(m)); }
static ABI int32_t posix_cond_init(GuestCond **c, void **a) { return posix_result(cond_init(c, a, NULL)); }
static ABI int32_t posix_cond_destroy(GuestCond **c) { return posix_result(cond_destroy(c)); }
static ABI int32_t posix_cond_wait(GuestCond **c, GuestMutex **m) { return posix_result(cond_wait(c, m)); }
static ABI int32_t posix_cond_timedwait(GuestCond **c, GuestMutex **m, const GuestTimespec *t) { return posix_result(cond_abs_timedwait(c, m, t)); }
static ABI int32_t posix_cond_signal(GuestCond **c) { return posix_result(cond_signal(c)); }
static ABI int32_t posix_cond_broadcast(GuestCond **c) { return posix_result(cond_broadcast(c)); }
uintptr_t runtime_mutex_resolve(const char *name) {
    if (!strcmp(name,"0TyVk4MSLt0")) return (uintptr_t)posix_cond_init;
    if (!strcmp(name,"RXXqi4CtF8w")) return (uintptr_t)posix_cond_destroy;
    if (!strcmp(name,"Op8TBGY5KHg")) return (uintptr_t)posix_cond_wait;
    if (!strcmp(name,"27bAgiJmOh0")) return (uintptr_t)posix_cond_timedwait;
    if (!strcmp(name,"2MOy+rUfuhQ")) return (uintptr_t)posix_cond_signal;
    if (!strcmp(name,"mkx2fVhNMsg")) return (uintptr_t)posix_cond_broadcast;
    if (!strcmp(name,"2Tb92quprl0")) return (uintptr_t)cond_init;
    if (!strcmp(name,"g+PZd2hiacg")) return (uintptr_t)cond_destroy;
    if (!strcmp(name,"WKAXJ4XBPQ4")) return (uintptr_t)cond_wait;
    if (!strcmp(name,"BmMjYxmew1w")) return (uintptr_t)cond_timedwait;
    if (!strcmp(name,"kDh-NfxgMtE")) return (uintptr_t)cond_signal;
    if (!strcmp(name,"JGgj7Uvrl+A")) return (uintptr_t)cond_broadcast;
    if (!strcmp(name,"IafI2PxcPnQ")) return (uintptr_t)mutex_timedlock;
    if (!strcmp(name,"dQHWEsJtoE4")) return (uintptr_t)posix_attr_init;
    if (!strcmp(name,"mDmgMOGVUqg")) return (uintptr_t)posix_attr_type;
    if (!strcmp(name,"HF7lK46xzjY")) return (uintptr_t)posix_attr_destroy;
    if (!strcmp(name,"ttHNfU+qDBU")) return (uintptr_t)posix_mutex_init;
    if (!strcmp(name,"ltCfaGr2JGE")) return (uintptr_t)posix_mutex_destroy;
    if (!strcmp(name,"7H0iTOciTLo")) return (uintptr_t)posix_mutex_lock;
    if (!strcmp(name,"K-jXhbt2gn4")) return (uintptr_t)posix_mutex_trylock;
    if (!strcmp(name,"2Z+PpY6CaJg")) return (uintptr_t)posix_mutex_unlock;
    if (!strcmp(name, "F8bUHwAG284")) return (uintptr_t)attr_init;
    if (!strcmp(name, "iMp8QpE+XO4")) return (uintptr_t)attr_type;
    if (!strcmp(name, "smWEktiyyG0")) return (uintptr_t)attr_destroy;
    if (!strcmp(name, "1FGvU0i9saQ")) return (uintptr_t)attr_protocol;
    if (!strcmp(name, "cmo1RIYva9o")) return (uintptr_t)mutex_init;
    if (!strcmp(name, "9UK1vLZQft4")) return (uintptr_t)mutex_lock;
    if (!strcmp(name, "upoVrzMHFeE")) return (uintptr_t)mutex_trylock;
    if (!strcmp(name, "tn3VlD0hG60")) return (uintptr_t)mutex_unlock;
    if (!strcmp(name, "2Of0f+3mhhE")) return (uintptr_t)mutex_destroy;
    return 0;
}
void runtime_mutex_report(void) {
#ifdef _WIN32
    printf("Runtime: mutexes created=%zu, condition variables created=%zu\n", created, conds);
#else
    printf("Runtime: mutexes created=%zu, locks=%zu, unlocks=%zu\n", created, locks, unlocks);
    printf("Runtime: condition variables created=%zu, waits=%zu, wakeups=%zu\n", conds, waits, wakeups);
#endif
}
