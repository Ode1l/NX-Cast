#include "template_resource.h"
#include "resource_store.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "log/log.h"

#define DLNA_TEMPLATE_FALLBACK_ROOT "./assets/dlna/"

static const char *template_lookup_placeholder(const DlnaTemplateValues *values, const char *name)
{
    if (!name)
        return "";

    if (strcmp(name, "url_base") == 0)
        return values && values->url_base ? values->url_base : "";
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

static bool template_placeholder_is_raw(const char *name)
{
    if (!name)
        return false;
    return strcmp(name, "header_extra") == 0 || strcmp(name, "service_extra") == 0;
}

static char *template_xml_escape_alloc(const char *value)
{
    const char *input = value ? value : "";
    size_t input_len = strlen(input);
    size_t out_capacity = input_len * 6 + 1;
    char *escaped = malloc(out_capacity);
    size_t used = 0;

    if (!escaped)
        return NULL;

    for (const char *cursor = input; *cursor; ++cursor)
    {
        const char *replacement = NULL;
        char literal[2] = {0};

        switch (*cursor)
        {
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = "&lt;";
            break;
        case '>':
            replacement = "&gt;";
            break;
        case '\"':
            replacement = "&quot;";
            break;
        case '\'':
            replacement = "&apos;";
            break;
        default:
            literal[0] = *cursor;
            replacement = literal;
            break;
        }

        size_t replacement_len = strlen(replacement);
        memcpy(escaped + used, replacement, replacement_len);
        used += replacement_len;
    }

    escaped[used] = '\0';
    return escaped;
}

static char *template_strdup_printf(const char *fmt, ...)
{
    va_list args;
    va_list args_copy;
    int needed;
    char *buffer;

    if (!fmt)
        return NULL;

    va_start(args, fmt);
    va_copy(args_copy, args);
    needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (needed < 0)
    {
        va_end(args);
        return NULL;
    }

    buffer = malloc((size_t)needed + 1);
    if (!buffer)
    {
        va_end(args);
        return NULL;
    }

    vsnprintf(buffer, (size_t)needed + 1, fmt, args);
    va_end(args);
    return buffer;
}

static FILE *template_open_file(const char *relative_path)
{
    const char *const roots[] = {
        DLNA_STORAGE_DLNA_DIR "/",
        DLNA_TEMPLATE_FALLBACK_ROOT
    };
    if (!relative_path)
        return NULL;

    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i)
    {
        char *resolved_path = template_strdup_printf("%s%s", roots[i], relative_path);
        FILE *file = NULL;
        if (!resolved_path)
            return NULL;

        file = fopen(resolved_path, "rb");
        free(resolved_path);
        if (file)
            return file;
    }

    return NULL;
}

static bool append_bytes_alloc(char **out, size_t *used, size_t *capacity, const char *data, size_t data_len)
{
    char *next;
    size_t next_capacity;

    if (!out || !used || !capacity || !data)
        return false;

    if (*used + data_len + 1 <= *capacity)
    {
        memcpy(*out + *used, data, data_len);
        *used += data_len;
        (*out)[*used] = '\0';
        return true;
    }

    next_capacity = *capacity == 0 ? 512 : *capacity * 2;
    while (*used + data_len + 1 > next_capacity)
        next_capacity *= 2;

    next = realloc(*out, next_capacity);
    if (!next)
        return false;

    *out = next;
    *capacity = next_capacity;
    memcpy(*out + *used, data, data_len);
    *used += data_len;
    (*out)[*used] = '\0';
    return true;
}

bool dlna_template_render_file_alloc(const char *relative_path,
                                     const DlnaTemplateValues *values,
                                     char **out,
                                     size_t *out_len)
{
    FILE *file;
    char *rendered = NULL;
    char *token = NULL;
    size_t rendered_used = 0;
    size_t rendered_capacity = 0;
    size_t token_used = 0;
    size_t token_capacity = 0;

    if (!relative_path || !out)
        return false;

    *out = NULL;
    if (out_len)
        *out_len = 0;

    file = template_open_file(relative_path);
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
            char single = (char)ch;
            if (!append_bytes_alloc(&rendered, &rendered_used, &rendered_capacity, &single, 1))
                goto fail;
            continue;
        }

        token_used = 0;
        if (!append_bytes_alloc(&token, &token_used, &token_capacity, "", 0))
            goto fail;

        for (;;)
        {
            ch = fgetc(file);
            if (ch == EOF || ch == '}')
                break;

            char single = (char)ch;
            if (!append_bytes_alloc(&token, &token_used, &token_capacity, &single, 1))
                goto fail;
        }

        if (ch != '}')
        {
            if (!append_bytes_alloc(&rendered, &rendered_used, &rendered_capacity, "{", 1))
                goto fail;
            if (token_used > 0 && !append_bytes_alloc(&rendered, &rendered_used, &rendered_capacity, token, token_used))
                goto fail;
            break;
        }

        const char *replacement = template_lookup_placeholder(values, token ? token : "");
        if (replacement)
        {
            const char *replacement_to_append = replacement;
            char *escaped = NULL;

            if (!template_placeholder_is_raw(token ? token : ""))
            {
                escaped = template_xml_escape_alloc(replacement);
                if (!escaped)
                    goto fail;
                replacement_to_append = escaped;
            }

            if (!append_bytes_alloc(&rendered, &rendered_used, &rendered_capacity, replacement_to_append, strlen(replacement_to_append)))
            {
                free(escaped);
                goto fail;
            }
            free(escaped);
            continue;
        }

        if (!append_bytes_alloc(&rendered, &rendered_used, &rendered_capacity, "{", 1) ||
            (token_used > 0 && !append_bytes_alloc(&rendered, &rendered_used, &rendered_capacity, token, token_used)) ||
            !append_bytes_alloc(&rendered, &rendered_used, &rendered_capacity, "}", 1))
        {
            goto fail;
        }
    }

    fclose(file);
    free(token);
    if (!rendered)
    {
        rendered = strdup("");
        if (!rendered)
            return false;
    }

    *out = rendered;
    if (out_len)
        *out_len = rendered_used;
    return true;

fail:
    fclose(file);
    free(token);
    free(rendered);
    return false;
}

bool dlna_template_load_file_alloc(const char *relative_path,
                                   char **out,
                                   size_t *out_len)
{
    FILE *file;
    char *buffer = NULL;
    size_t used = 0;
    size_t capacity = 0;

    if (!relative_path || !out)
        return false;

    *out = NULL;
    if (out_len)
        *out_len = 0;

    file = template_open_file(relative_path);
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
