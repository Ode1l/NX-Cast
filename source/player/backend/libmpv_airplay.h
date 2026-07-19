#ifndef NXCAST_PLAYER_BACKEND_LIBMPV_AIRPLAY_H
#define NXCAST_PLAYER_BACKEND_LIBMPV_AIRPLAY_H

#include <stdbool.h>

typedef struct AirPlayStreamBridge AirPlayStreamBridge;

#define PLAYER_LIBMPV_AIRPLAY_URI "airplay://mirror"

/* The backend retains bridge until it is replaced or libmpv is destroyed. */
bool player_libmpv_set_airplay_stream_bridge(AirPlayStreamBridge *bridge);

#endif
