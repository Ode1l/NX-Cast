#include "player/ingress/model.h"

#include <stdio.h>
#include <string.h>

#include "player/ingress/classify.h"

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

void ingress_model_init(IngressModel *model, const char *uri, const char *metadata, const PlayerOpenContext *ctx)
{
    bool likely_segmented = false;

    if (!model)
        return;

    memset(model, 0, sizeof(*model));
    ingress_collect_evidence(uri, metadata, ctx, &model->evidence);

    snprintf(model->input_uri, sizeof(model->input_uri), "%s", uri ? uri : "");
    snprintf(model->resolved_uri, sizeof(model->resolved_uri), "%s", uri ? uri : "");
    if (metadata)
        snprintf(model->metadata, sizeof(model->metadata), "%s", metadata);
    if (model->evidence.metadata_mime[0] != '\0')
        snprintf(model->mime_type, sizeof(model->mime_type), "%s", model->evidence.metadata_mime);

    model->format = ingress_classify_format(model->resolved_uri,
                                            model->mime_type[0] != '\0' ? model->mime_type : model->metadata,
                                            &likely_segmented);
    model->likely_segmented = likely_segmented;
    model->hint_vendor = model->evidence.sender_vendor;
    model->detected_vendor = ingress_classify_vendor(model->resolved_uri,
                                                     model->input_uri,
                                                     model->metadata,
                                                     &model->evidence);
    model->vendor = model->detected_vendor;
    model->transport = ingress_classify_transport(model->resolved_uri,
                                                  model->format,
                                                  model->likely_segmented,
                                                  model->vendor);
}

void ingress_model_finalize(IngressModel *model)
{
    if (!model)
        return;

    model->vendor = ingress_classify_vendor(model->resolved_uri,
                                            model->input_uri,
                                            model->metadata,
                                            &model->evidence);
    model->transport = ingress_classify_transport(model->resolved_uri,
                                                  model->format,
                                                  model->likely_segmented,
                                                  model->vendor);
}

void ingress_model_apply_to_media(const IngressModel *model, PlayerMedia *media)
{
    if (!model || !media)
        return;

    snprintf(media->uri, sizeof(media->uri), "%s", model->resolved_uri);
    snprintf(media->original_uri, sizeof(media->original_uri), "%s", model->input_uri);
    snprintf(media->metadata, sizeof(media->metadata), "%s", model->metadata);
    snprintf(media->protocol_info, sizeof(media->protocol_info), "%s", model->protocol_info);
    snprintf(media->mime_type, sizeof(media->mime_type), "%s", model->mime_type);
    media->format = model->format;
    media->transport = model->transport;
    media->vendor = model->vendor;
    media->selected_from_metadata = model->selected_from_metadata;
    media->metadata_candidate_count = model->metadata_candidate_count;
    ingress_apply_classification(media, model->likely_segmented, &model->evidence);

    if (model->range_support_known &&
        !model->range_seekable &&
        media->format != PLAYER_MEDIA_FORMAT_HLS)
    {
        append_load_option(media->mpv_load_options,
                           sizeof(media->mpv_load_options),
                           "stream-lavf-o=seekable=0");
        append_load_option(media->mpv_load_options,
                           sizeof(media->mpv_load_options),
                           "demuxer-seekable-cache=no");
    }
}
