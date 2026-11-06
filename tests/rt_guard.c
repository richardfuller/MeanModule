/* LD_PRELOAD guard: counts malloc/free/lock calls made while rt_guard_enter() is active. */
#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <stddef.h>
extern void *__libc_malloc(size_t), *__libc_calloc(size_t, size_t), *__libc_realloc(void *, size_t);
extern void __libc_free(void *);
static __thread int g_in_rt;
static volatile int g_violations;
void rt_guard_enter(void) { g_in_rt = 1; }
void rt_guard_leave(void) { g_in_rt = 0; }
int rt_guard_count(void) { return g_violations; }
#define HIT() do { if (g_in_rt) __sync_fetch_and_add(&g_violations, 1); } while (0)
void *malloc(size_t n) { HIT(); return __libc_malloc(n); }
void *calloc(size_t a, size_t b) { HIT(); return __libc_calloc(a, b); }
void *realloc(void *p, size_t n) { HIT(); return __libc_realloc(p, n); }
void free(void *p) { if (p) HIT(); __libc_free(p); }
int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; HIT(); return 0; }
