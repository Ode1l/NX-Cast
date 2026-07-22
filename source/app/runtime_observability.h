#ifndef NXCAST_APP_RUNTIME_OBSERVABILITY_H
#define NXCAST_APP_RUNTIME_OBSERVABILITY_H

#include <stdint.h>

#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY

#include "log/log.h"
#include "runtime_diagnostics.h"

static inline void runtime_observability_log_resources(
    const char *event, const char *owner, uint32_t generation)
{
    RuntimeDiagnosticResourceSnapshot snapshot;
    char detail[1536];

    if (!runtime_diagnostics_collect_resources(&snapshot))
        return;
    if (!runtime_diagnostics_format_resource_snapshot(
            &snapshot, event, owner, generation, detail, sizeof(detail)))
    {
        log_warn("[resource-snapshot] event=%s owner=%s generation=%u "
                 "status=truncated\n",
                 event ? event : "unknown", owner ? owner : "unknown",
                 generation);
        return;
    }
    log_info("[resource-snapshot] %s\n", detail);
}

#else

static inline void runtime_observability_log_resources(
    const char *event, const char *owner, uint32_t generation)
{
    (void)event;
    (void)owner;
    (void)generation;
}

#endif

#endif
