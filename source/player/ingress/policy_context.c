#include "player/policy.h"

#include <stdio.h>
#include <string.h>

#include "player/ingress/vendor.h"

static void append_header_field(char *out, size_t out_size, const char *name, const char *value)
{
    size_t used;

    if (!out || out_size == 0 || !name || !value || value[0] == '\0')
        return;

    used = strlen(out);
    if (used >= out_size - 1)
        return;

    snprintf(out + used,
             out_size - used,
             "%s%s: %s",
             used > 0 ? "," : "",
             name,
             value);
}

static void append_raw_headers(char *out, size_t out_size, const char *headers)
{
    size_t used;

    if (!out || out_size == 0 || !headers || headers[0] == '\0')
        return;

    used = strlen(out);
    if (used >= out_size - 1)
        return;

    snprintf(out + used,
             out_size - used,
             "%s%s",
             used > 0 ? "," : "",
             headers);
}

void policy_refresh_header_fields(PlayerMedia *media)
{
    if (!media)
        return;

    media->header_fields[0] = '\0';

    append_header_field(media->header_fields, sizeof(media->header_fields), "Referer", media->referrer);
    append_header_field(media->header_fields, sizeof(media->header_fields), "Origin", media->origin);
    append_header_field(media->header_fields, sizeof(media->header_fields), "User-Agent", media->user_agent);
    append_header_field(media->header_fields, sizeof(media->header_fields), "Cookie", media->cookie);
    append_raw_headers(media->header_fields, sizeof(media->header_fields), media->extra_headers);
}

void policy_apply_request_context(PlayerMedia *media, const PlayerOpenContext *ctx)
{
    if (!media || !ctx)
        return;

    if (ctx->sender_user_agent[0] != '\0')
    {
        snprintf(media->sender_user_agent, sizeof(media->sender_user_agent), "%s", ctx->sender_user_agent);
        if (media->flags.is_local_proxy)
            snprintf(media->user_agent, sizeof(media->user_agent), "%s", ctx->sender_user_agent);
    }

    if (ctx->referrer[0] != '\0' && ingress_vendor_is_sensitive(media->vendor))
        snprintf(media->referrer, sizeof(media->referrer), "%s", ctx->referrer);

    if (ctx->origin[0] != '\0' && ingress_vendor_is_sensitive(media->vendor))
        snprintf(media->origin, sizeof(media->origin), "%s", ctx->origin);

    if (ctx->cookie[0] != '\0')
        snprintf(media->cookie, sizeof(media->cookie), "%s", ctx->cookie);

    if (ctx->extra_headers[0] != '\0')
        snprintf(media->extra_headers, sizeof(media->extra_headers), "%s", ctx->extra_headers);

    policy_refresh_header_fields(media);
}
