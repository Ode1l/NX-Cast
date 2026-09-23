#pragma once
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

typedef uint32_t u32;
typedef int Result;
typedef pthread_mutex_t Mutex;
typedef pthread_cond_t CondVar;
typedef struct { int handle; } Thread;
typedef struct { int unused; } SwkbdConfig;
#define R_FAILED(rc) ((rc) != 0)
#define R_SUCCEEDED(rc) ((rc) == 0)
static inline void mutexInit(Mutex *m) { pthread_mutex_init(m, NULL); }
static inline void mutexLock(Mutex *m) { pthread_mutex_lock(m); }
static inline void mutexUnlock(Mutex *m) { pthread_mutex_unlock(m); }
static inline void condvarInit(CondVar *c) { pthread_cond_init(c, NULL); }
static inline void condvarWakeOne(CondVar *c) { pthread_cond_signal(c); }
static inline void condvarWakeAll(CondVar *c) { pthread_cond_broadcast(c); }
static inline void condvarWait(CondVar *c, Mutex *m) { pthread_cond_wait(c, m); }
static inline int condvarWaitTimeout(CondVar *c, Mutex *m, uint64_t t)
{ (void)c; (void)m; (void)t; return 1; }
/* These focused data tests intentionally disable background network jobs. */
static inline Result threadCreate(Thread *t, void (*fn)(void *), void *a,
                                  void *s, size_t n, int p, int cpu)
{ (void)t; (void)fn; (void)a; (void)s; (void)n; (void)p; (void)cpu; return 1; }
static inline Result threadStart(Thread *t) { (void)t; return 1; }
static inline void threadClose(Thread *t) { (void)t; }
static inline void threadWaitForExit(Thread *t) { (void)t; }
static inline void threadExit(void) {}
static inline uint64_t armGetSystemTick(void) { return 0; }
static inline uint64_t armTicksToNs(uint64_t t) { return t; }
static inline Result swkbdCreate(SwkbdConfig *k, int n) { (void)k; (void)n; return 1; }
static inline void swkbdConfigMakePresetDefault(SwkbdConfig *k) { (void)k; }
static inline void swkbdConfigSetHeaderText(SwkbdConfig *k, const char *s) { (void)k; (void)s; }
static inline void swkbdConfigSetSubText(SwkbdConfig *k, const char *s) { (void)k; (void)s; }
static inline void swkbdConfigSetGuideText(SwkbdConfig *k, const char *s) { (void)k; (void)s; }
static inline void swkbdConfigSetOkButtonText(SwkbdConfig *k, const char *s) { (void)k; (void)s; }
static inline void swkbdConfigSetInitialText(SwkbdConfig *k, const char *s) { (void)k; (void)s; }
static inline void swkbdConfigSetStringLenMax(SwkbdConfig *k, u32 n) { (void)k; (void)n; }
static inline Result swkbdShow(SwkbdConfig *k, char *s, size_t n) { (void)k; (void)s; (void)n; return 1; }
static inline void swkbdClose(SwkbdConfig *k) { (void)k; }
