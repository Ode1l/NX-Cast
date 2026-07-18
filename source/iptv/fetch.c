#include "iptv/fetch.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>

static void fetch_set_error(char *out, size_t out_size, const char *message)
{
    if (!out || out_size == 0)
        return;
    snprintf(out, out_size, "%s", message ? message : "download failed");
}

bool iptv_fetch_to_file(const char *url,
                        const char *destination,
                        size_t maximum_size,
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
    int rc;

    if (!url || !url[0] || !destination || !destination[0] || maximum_size == 0)
    {
        fetch_set_error(error, error_size, "invalid download arguments");
        return false;
    }

    snprintf(temporary, sizeof(temporary), "%s.tmp", destination);
    remove(temporary);
    av_dict_set(&options, "user_agent", "NX-Cast/0.1 IPTV", 0);
    av_dict_set(&options, "rw_timeout", "15000000", 0);
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "2", 0);

    rc = avio_open2(&input, url, AVIO_FLAG_READ, NULL, &options);
    av_dict_free(&options);
    if (rc < 0)
    {
        char av_error[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(rc, av_error, sizeof(av_error));
        fetch_set_error(error, error_size, av_error);
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
        if (total + (size_t)rc > maximum_size)
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
        ok = false;
    if (input)
        avio_closep(&input);
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
