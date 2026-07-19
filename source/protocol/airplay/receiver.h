#ifndef NXCAST_AIRPLAY_RECEIVER_H
#define NXCAST_AIRPLAY_RECEIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol/airplay/airplay.h"
#include "protocol/airplay/protocol/handlers.h"

typedef struct
{
    const char *friendly_name;
    const char *storage_directory;
    uint16_t control_port;
    uint64_t features;
    bool enable_discovery;
    AirPlayPinDisplayCallback pin_display_callback;
    AirPlayPinDismissCallback pin_dismiss_callback;
    void *pin_user_data;
    AirPlayKeyUnwrapCallback unwrap_key_callback;
    AirPlayTransportPrepareCallback transport_prepare_callback;
    AirPlayMirrorOpenCallback mirror_open_callback;
    AirPlayMirrorRecordCallback mirror_record_callback;
    AirPlayMirrorStopCallback mirror_stop_callback;
    void *media_user_data;
} AirPlayReceiverConfig;

bool airplay_receiver_start(const AirPlayReceiverConfig *config);
void airplay_receiver_stop(void);
bool airplay_receiver_is_running(void);
uint16_t airplay_receiver_port(void);

#endif
