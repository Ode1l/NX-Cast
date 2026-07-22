#include "iptv/fetch.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>

#include "app/network_diagnostics.h"

static int fetch_interrupted(void *opaque)
{
    const IptvFetchControl *control = opaque;

    return control && control->cancelled &&
           control->cancelled(control->context)
               ? 1
               : 0;
}

static void fetch_set_error(char *out, size_t out_size, const char *message)
{
    if (!out || out_size == 0)
        return;
    snprintf(out, out_size, "%s", message ? message : "download failed");
}

bool iptv_fetch_to_file(const char *url,
                        const char *destination,
                        size_t maximum_size,
                        const IptvFetchControl *control,
                        char *error,
                        size_t error_size)
{
    AVIOContext *input = NULL;
    AVDictionary *options = NULL;
    FILE *output = NULL;
    char temporary[640];
    unsigned char buffer[32768];
    size_t total = 0;
    bool ok = false;
    bool cancelled = false;
    int rc;
    int written;
    AVIOInterruptCB interrupt = {
        .callback = fetch_interrupted,
        .opaque = (void *)control,
    };
    NetworkOperationToken operation = {0};

    if (error && error_size > 0u)
        error[0] = '\0';

    if (!url || !url[0] || !destination || !destination[0] || maximum_size == 0)
    {
        fetch_set_error(error, error_size, "invalid download arguments");
        return false;
    }

    written = snprintf(temporary, sizeof(temporary), "%s.tmp", destination);
    if (written < 0 || (size_t)written >= sizeof(temporary))
    {
        fetch_set_error(error, error_size, "cache path is too long");
        return false;
    }
    remove(temporary);
    av_dict_set(&options, "user_agent", "NX-Cast/0.1 IPTV", 0);
    av_dict_set(&options, "rw_timeout", "3000000", 0);
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "2", 0);

    operation = network_diagnostics_operation_begin(
        NETWORK_DIAGNOSTIC_IPTV_BACKGROUND, NETWORK_OPERATION_HTTP_FETCH);
    if (fetch_interrupted((void *)control))
    {
        cancelled = true;
        fetch_set_error(error, error_size, "download cancelled");
        rc = AVERROR_EXIT;
    }
    else
    {
        rc = avio_open2(&input, url, AVIO_FLAG_READ, &interrupt, &options);
    }
    av_dict_free(&options);
    if (rc < 0)
    {
        char av_error[AV_ERROR_MAX_STRING_SIZE];
        cancelled = fetch_interrupted((void *)control) != 0;
        if (!cancelled)
        {
            av_strerror(rc, av_error, sizeof(av_error));
            fetch_set_error(error, error_size, av_error);
        }
        network_diagnostics_operation_end(
            &operation, cancelled ? ECANCELED : EIO);
        return false;
    }

    output = fopen(temporary, "wb");
    if (!output)
    {
        char io_error[96];
        snprintf(io_error, sizeof(io_error), "cannot create cache file: errno=%d", errno);
        fetch_set_error(error, error_size, io_error);
        goto cleanup;
    }

    for (;;)
    {
        if (fetch_interrupted((void *)control))
        {
            cancelled = true;
            fetch_set_error(error, error_size, "download cancelled");
            break;
        }
        rc = avio_read(input, buffer, (int)sizeof(buffer));
        if (rc == AVERROR_EOF)
        {
            ok = total > 0;
            break;
        }
        if (rc < 0)
        {
            char av_error[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(rc, av_error, sizeof(av_error));
            fetch_set_error(error, error_size, av_error);
            break;
        }
        if (rc == 0)
            continue;
        if (total > maximum_size || (size_t)rc > maximum_size - total)
        {
            fetch_set_error(error, error_size, "download exceeds cache size limit");
            break;
        }
        if (fwrite(buffer, 1, (size_t)rc, output) != (size_t)rc)
        {
            fetch_set_error(error, error_size, "failed to write cache file");
            break;
        }
        total += (size_t)rc;
    }

cleanup:
    if (output && fclose(output) != 0)
    {
        ok = false;
        fetch_set_error(error, error_size, "failed to close cache file");
    }
    if (input)
        avio_closep(&input);
    network_diagnostics_operation_end(
        &operation, ok ? 0 : (cancelled ? ECANCELED : EIO));
    if (!ok)
    {
        remove(temporary);
        if (error && error_size > 0 && !error[0])
            fetch_set_error(error, error_size, "empty response");
        return false;
    }

    remove(destination);
    if (rename(temporary, destination) != 0)
    {
        remove(temporary);
        fetch_set_error(error, error_size, "failed to install cache file");
        return false;
    }

    if (error && error_size > 0)
        error[0] = '\0';
    return true;
}
