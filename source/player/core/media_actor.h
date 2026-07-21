#ifndef NXCAST_PLAYER_CORE_MEDIA_ACTOR_H
#define NXCAST_PLAYER_CORE_MEDIA_ACTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEDIA_ACTOR_WAIT_FOREVER UINT32_MAX

typedef struct MediaActor MediaActor;

typedef enum
{
    MEDIA_ACTOR_COMMAND_OPEN = 1,
    MEDIA_ACTOR_COMMAND_PLAY,
    MEDIA_ACTOR_COMMAND_PAUSE,
    MEDIA_ACTOR_COMMAND_STOP,
    MEDIA_ACTOR_COMMAND_STOP_ANY,
    MEDIA_ACTOR_COMMAND_SEEK_TARGET,
    MEDIA_ACTOR_COMMAND_SEEK_MS,
    MEDIA_ACTOR_COMMAND_SET_VOLUME,
    MEDIA_ACTOR_COMMAND_SET_MUTE,
    MEDIA_ACTOR_COMMAND_SHOW_OSD,
    MEDIA_ACTOR_COMMAND_BIND_STREAM,
    MEDIA_ACTOR_COMMAND_RELEASE_LEASE,
    MEDIA_ACTOR_COMMAND_QUIESCE,
    MEDIA_ACTOR_COMMAND_SHUTDOWN
} MediaActorCommandKind;

typedef enum
{
    MEDIA_ACTOR_PRODUCER_UNKNOWN = 0,
    MEDIA_ACTOR_PRODUCER_APP,
    MEDIA_ACTOR_PRODUCER_UI,
    MEDIA_ACTOR_PRODUCER_DLNA,
    MEDIA_ACTOR_PRODUCER_IPTV,
    MEDIA_ACTOR_PRODUCER_AIRPLAY_VIDEO,
    MEDIA_ACTOR_PRODUCER_AIRPLAY_MIRROR,
    MEDIA_ACTOR_PRODUCER_TEST
} MediaActorProducer;

typedef void (*MediaActorOpaqueFn)(void *opaque);

typedef struct
{
    uint64_t command_id;
    MediaActorCommandKind kind;
    MediaActorProducer producer;
    uint64_t session_token;
    uint32_t generation;
    const char *text;
    const char *metadata;
    int value;
    bool flag;
    void *opaque;
    MediaActorOpaqueFn opaque_retain;
    MediaActorOpaqueFn opaque_release;
} MediaActorCommand;

typedef enum
{
    MEDIA_ACTOR_SUBMIT_ACCEPTED = 0,
    MEDIA_ACTOR_SUBMIT_EXECUTED,
    MEDIA_ACTOR_SUBMIT_EXECUTION_FAILED,
    MEDIA_ACTOR_SUBMIT_INVALID,
    MEDIA_ACTOR_SUBMIT_NOT_RUNNING,
    MEDIA_ACTOR_SUBMIT_QUEUE_FULL,
    MEDIA_ACTOR_SUBMIT_NO_MEMORY,
    MEDIA_ACTOR_SUBMIT_TIMEOUT,
    MEDIA_ACTOR_SUBMIT_SHUTTING_DOWN,
    MEDIA_ACTOR_SUBMIT_STALE
} MediaActorSubmitStatus;

typedef bool (*MediaActorValidateFn)(void *context,
                                     const MediaActorCommand *command);
typedef bool (*MediaActorExecuteFn)(void *context,
                                    const MediaActorCommand *command);
typedef bool (*MediaActorInitializeFn)(void *context);
typedef void (*MediaActorFinalizeFn)(void *context);
typedef void (*MediaActorPumpEventsFn)(void *context, int timeout_ms);
typedef void (*MediaActorWakeupFn)(void *context);

typedef struct
{
    size_t capacity;
    size_t reserved_capacity;
    size_t max_command_burst;
    uint32_t idle_poll_ms;
    size_t thread_stack_size;
    int thread_priority;
    int thread_core;
    bool start_suspended;
    MediaActorInitializeFn initialize;
    MediaActorFinalizeFn finalize;
    MediaActorValidateFn validate;
    MediaActorExecuteFn execute;
    MediaActorPumpEventsFn pump_events;
    MediaActorWakeupFn wakeup;
    void *context;
} MediaActorConfig;

typedef struct
{
    bool running;
    bool stopping;
    bool accepting;
    bool initializing;
    bool ready;
    bool initialization_failed;
    bool dispatch_enabled;
    size_t pending;
    size_t max_depth;
    uint64_t submitted;
    uint64_t executed;
    uint64_t execution_failed;
    uint64_t rejected_full;
    uint64_t rejected_stopping;
    uint64_t rejected_stale;
    uint64_t coalesced;
    uint64_t timed_out;
    uint64_t last_command_id;
    uint64_t current_command_id;
    MediaActorCommandKind current_command_kind;
    MediaActorProducer current_command_producer;
    uint64_t current_session_token;
    uint32_t current_generation;
    uint64_t last_completed_command_id;
    uint64_t current_command_age_ms;
    uint64_t heartbeat_age_ms;
} MediaActorHealth;

MediaActor *media_actor_create(const MediaActorConfig *config);
bool media_actor_start(MediaActor *actor);
bool media_actor_activate(MediaActor *actor);
void media_actor_quiesce(MediaActor *actor);
bool media_actor_wait_idle(MediaActor *actor, uint32_t timeout_ms);
void media_actor_stop(MediaActor *actor);
void media_actor_destroy(MediaActor *actor);

MediaActorSubmitStatus media_actor_submit_async(
    MediaActor *actor, const MediaActorCommand *command);
MediaActorSubmitStatus media_actor_submit_wait(
    MediaActor *actor, const MediaActorCommand *command, uint32_t timeout_ms);

bool media_actor_is_current_thread(MediaActor *actor);
bool media_actor_get_health(MediaActor *actor, MediaActorHealth *out);
const char *media_actor_submit_status_name(MediaActorSubmitStatus status);

#endif
