#ifndef NXCAST_AIRPLAY_TRACE_H
#define NXCAST_AIRPLAY_TRACE_H

#if defined(NXCAST_AIRPLAY_TRACE_VERBOSE) && NXCAST_AIRPLAY_TRACE_VERBOSE
#ifdef __SWITCH__
#include "log/log.h"
#define AIRPLAY_TRACE(...) log_info(__VA_ARGS__)
#define AIRPLAY_TRACE_WARN(...) log_warn(__VA_ARGS__)
#else
#include <stdio.h>
#define AIRPLAY_TRACE(...) ((void)fprintf(stderr, __VA_ARGS__))
#define AIRPLAY_TRACE_WARN(...) ((void)fprintf(stderr, __VA_ARGS__))
#endif
#else
#define AIRPLAY_TRACE(...) ((void)0)
#define AIRPLAY_TRACE_WARN(...) ((void)0)
#endif

#endif
