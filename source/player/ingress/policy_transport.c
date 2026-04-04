#include "player/policy.h"

#include <stdio.h>
#include <string.h>

#include "player/ingress/vendor.h"

#define SOURCE_POLICY_LOCAL_PROXY_LOAD_OPTIONS \
    "hls-bitrate=min,cache-pause-initial=yes,cache-pause-wait=2,demuxer-seekable-cache=no"

static void append_load_option(char *options, size_t options_size, const char *value)
{
    size_t used;

    if (!options || options_size == 0 || !value || value[0] == '\0')
        return;

    used = strlen(options);
    if (used >= options_size - 1)
        return;

    snprintf(options + used,
             options_size - used,
             "%s%s",
             used > 0 ? "," : "",
             value);
}

void policy_apply_transport(PlayerMedia *media, const PlayerOpenContext *ctx)
{
    if (!media)
        return;

    if (media->flags.is_local_proxy && media->flags.is_hls)
    {
        media->network_timeout_seconds = 12;
        media->demuxer_readahead_seconds = 2;
        append_load_option(media->mpv_load_options,
                           sizeof(media->mpv_load_options),
                           SOURCE_POLICY_LOCAL_PROXY_LOAD_OPTIONS);

        if (ctx && ctx->sender_user_agent[0] != '\0')
            snprintf(media->user_agent, sizeof(media->user_agent), "%s", ctx->sender_user_agent);

        if (ingress_vendor_is_sensitive(media->vendor))
        {
            snprintf(media->format_hint,
                     sizeof(media->format_hint),
                     "%s-hls-local-proxy",
                     ingress_vendor_name(media->vendor));
        }
        else
        {
            snprintf(media->format_hint,
                     sizeof(media->format_hint),
                     "%s",
                     "hls-local-proxy");
        }

        policy_refresh_header_fields(media);
    }
}
