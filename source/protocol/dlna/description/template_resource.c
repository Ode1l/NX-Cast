#include "template_resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log/log.h"

#define DLNA_TEMPLATE_PRIMARY_ROOT "romfs:/dlna/"
#define DLNA_TEMPLATE_FALLBACK_ROOT "./romfs/dlna/"

static const char *template_lookup_placeholder(const DlnaTemplateValues *values, const char *name)
{
    if (!name)
        return "";

    if (strcmp(name, "friendly_name") == 0)
        return values && values->friendly_name ? values->friendly_name : "";
    if (strcmp(name, "manufacturer") == 0)
        return values && values->manufacturer ? values->manufacturer : "";
    if (strcmp(name, "manufacturer_url") == 0)
        return values && values->manufacturer_url ? values->manufacturer_url : "";
    if (strcmp(name, "model_description") == 0)
        return values && values->model_description ? values->model_description : "";
    if (strcmp(name, "model_name") == 0)
        return values && values->model_name ? values->model_name : "";
    if (strcmp(name, "model_number") == 0)
        return values && values->model_number ? values->model_number : "";
    if (strcmp(name, "model_url") == 0)
        return values && values->model_url ? values->model_url : "";
    if (strcmp(name, "serial_num") == 0)
        return values && values->serial_number ? values->serial_number : "";
    if (strcmp(name, "uuid") == 0)
        return values && values->uuid ? values->uuid : "";
    if (strcmp(name, "header_extra") == 0)
        return values && values->header_extra ? values->header_extra : "";
    if (strcmp(name, "service_extra") == 0)
        return values && values->service_extra ? values->service_extra : "";
    return NULL;
}

static FILE *template_open_file(const char *relative_path, char *resolved_path, size_t resolved_path_size)
{
    static const char *const roots[] = {
        DLNA_TEMPLATE_PRIMARY_ROOT,
        DLNA_TEMPLATE_FALLBACK_ROOT
    };

    if (!relative_path || !resolved_path || resolved_path_size == 0)
        return NULL;

    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i)
    {
        snprintf(resolved_path, resolved_path_size, "%s%s", roots[i], relative_path);
        FILE *file = fopen(resolved_path, "rb");
        if (file)
            return file;
    }

    return NULL;
}

static bool append_bytes(char *out, size_t out_size, size_t *used, const char *data, size_t data_len)
{
    if (!out || !used || !data)
        return false;

    if (*used + data_len >= out_size)
        return false;

    memcpy(out + *used, data, data_len);
    *used += data_len;
    out[*used] = '\0';
    return true;
}

bool dlna_template_render_file_to_buffer(const char *relative_path,
                                         const DlnaTemplateValues *values,
                                         char *out,
                                         size_t out_size,
                                         size_t *out_len)
{
    char resolved_path[256];
    char token[128];
    FILE *file;
    size_t used = 0;

    if (!relative_path || !out || out_size == 0 || !out_len)
        return false;

    out[0] = '\0';
    *out_len = 0;

    file = template_open_file(relative_path, resolved_path, sizeof(resolved_path));
    if (!file)
    {
        log_error("[template] failed to open %s\n", relative_path);
        return false;
    }

    for (;;)
    {
        int ch = fgetc(file);
        if (ch == EOF)
            break;

        if (ch != '{')
        {
            char single[2] = {(char)ch, '\0'};
            if (!append_bytes(out, out_size, &used, single, 1))
                goto fail_too_large;
            continue;
        }

        size_t token_len = 0;
        bool closed = false;
        while (token_len + 1 < sizeof(token))
        {
            ch = fgetc(file);
            if (ch == EOF)
                break;
            if (ch == '}')
            {
                closed = true;
                break;
            }
            token[token_len++] = (char)ch;
        }
        token[token_len] = '\0';

        if (!closed)
        {
            if (!append_bytes(out, out_size, &used, "{", 1))
                goto fail_too_large;
            if (token_len > 0 && !append_bytes(out, out_size, &used, token, token_len))
                goto fail_too_large;
            break;
        }

        const char *replacement = template_lookup_placeholder(values, token);
        if (replacement)
        {
            if (!append_bytes(out, out_size, &used, replacement, strlen(replacement)))
                goto fail_too_large;
            continue;
        }

        if (!append_bytes(out, out_size, &used, "{", 1) ||
            (token_len > 0 && !append_bytes(out, out_size, &used, token, token_len)) ||
            !append_bytes(out, out_size, &used, "}", 1))
        {
            goto fail_too_large;
        }
    }

    fclose(file);
    *out_len = used;
    return true;

fail_too_large:
    fclose(file);
    log_error("[template] rendered %s exceeds buffer size=%zu\n", relative_path, out_size);
    return false;
}

bool dlna_template_load_file_alloc(const char *relative_path,
                                   char **out,
                                   size_t *out_len)
{
    char resolved_path[256];
    FILE *file;
    char *buffer = NULL;
    size_t used = 0;
    size_t capacity = 0;

    if (!relative_path || !out)
        return false;

    *out = NULL;
    if (out_len)
        *out_len = 0;

    file = template_open_file(relative_path, resolved_path, sizeof(resolved_path));
    if (!file)
    {
        log_error("[template] failed to open %s\n", relative_path);
        return false;
    }

    while (!feof(file))
    {
        if (used + 256 + 1 > capacity)
        {
            size_t next_capacity = capacity == 0 ? 512 : capacity * 2;
            while (used + 256 + 1 > next_capacity)
                next_capacity *= 2;

            char *next_buffer = realloc(buffer, next_capacity);
            if (!next_buffer)
            {
                free(buffer);
                fclose(file);
                return false;
            }

            buffer = next_buffer;
            capacity = next_capacity;
        }

        size_t chunk = fread(buffer + used, 1, capacity - used - 1, file);
        used += chunk;

        if (ferror(file))
        {
            free(buffer);
            fclose(file);
            return false;
        }

        if (chunk == 0)
            break;
    }

    fclose(file);

    if (!buffer)
    {
        buffer = malloc(1);
        if (!buffer)
            return false;
    }

    buffer[used] = '\0';
    *out = buffer;
    if (out_len)
        *out_len = used;
    return true;
}
