#ifndef NXCAST_AIRPLAY_TRACE_H
#define NXCAST_AIRPLAY_TRACE_H

#if defined(NXCAST_AIRPLAY_TRACE_VERBOSE) && NXCAST_AIRPLAY_TRACE_VERBOSE
#include <stdint.h>
#include <stdio.h>

#ifdef __SWITCH__
#include <switch.h>

#include "log/log.h"

static inline uint64_t airplay_trace_now_ms(void)
{
    return armTicksToNs(armGetSystemTick()) / UINT64_C(1000000);
}

#define AIRPLAY_TRACE(...) log_info(__VA_ARGS__)
#define AIRPLAY_TRACE_WARN(...) log_warn(__VA_ARGS__)
#define AIRPLAY_TRACE_SYNC(...) log_info(__VA_ARGS__)
#else
#include <time.h>

static inline uint64_t airplay_trace_now_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

#define AIRPLAY_TRACE(...) ((void)fprintf(stderr, __VA_ARGS__))
#define AIRPLAY_TRACE_WARN(...) ((void)fprintf(stderr, __VA_ARGS__))
#define AIRPLAY_TRACE_SYNC(...)                                                 \
    do                                                                          \
    {                                                                           \
        (void)fprintf(stderr, __VA_ARGS__);                                      \
        (void)fflush(stderr);                                                    \
    } while (0)
#endif
#define AIRPLAY_TRACE_NOW_MS() airplay_trace_now_ms()
#else
#define AIRPLAY_TRACE(...) ((void)0)
#define AIRPLAY_TRACE_WARN(...) ((void)0)
#define AIRPLAY_TRACE_SYNC(...) ((void)0)
#define AIRPLAY_TRACE_NOW_MS() 0ULL
#endif

#endif
