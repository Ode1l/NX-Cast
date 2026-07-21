#include "iptv/url.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "util/size.h"

static bool url_is_remote(const char *url)
{
    return url && (strncasecmp(url, "http://", 7u) == 0 ||
                   strncasecmp(url, "https://", 8u) == 0);
}

static bool url_has_uri_scheme(const char *url)
{
    const char *separator;

    if (!url || !url[0])
        return false;
    if (strncasecmp(url, "sdmc:/", 6u) == 0)
        return true;
    separator = strstr(url, "://");
    if (!separator || separator == url)
        return false;
    for (const char *cursor = url; cursor < separator; ++cursor)
    {
        if (!isalnum((unsigned char)*cursor) && *cursor != '+' &&
            *cursor != '-' && *cursor != '.')
        {
            return false;
        }
    }
    return true;
}

static bool url_write_parts(char *output, size_t output_size,
                            const char *first, size_t first_size,
                            const char *second, size_t second_size,
                            const char *third, size_t third_size)
{
    size_t total;
    size_t used = 0u;

    if (!output || output_size == 0u ||
        (first_size > 0u && !first) ||
        (second_size > 0u && !second) ||
        (third_size > 0u && !third))
    {
        return false;
    }
    output[0] = '\0';
    if (!nxcast_size_add(first_size, second_size, &total) ||
        !nxcast_size_add(total, third_size, &total) || total >= output_size)
    {
        return false;
    }
    if (first_size > 0u)
    {
        memcpy(output + used, first, first_size);
        used += first_size;
    }
    if (second_size > 0u)
    {
        memcpy(output + used, second, second_size);
        used += second_size;
    }
    if (third_size > 0u)
    {
        memcpy(output + used, third, third_size);
        used += third_size;
    }
    output[used] = '\0';
    return true;
}

static bool url_copy(char *output, size_t output_size, const char *value)
{
    return value && url_write_parts(output, output_size,
                                    value, strlen(value), NULL, 0u, NULL, 0u);
}

bool iptv_url_resolve(const char *base, const char *reference,
                      char *output, size_t output_size)
{
    const char *base_end;
    const char *scheme_end;
    const char *authority_start;
    const char *path_start;
    const char *last_slash = NULL;
    size_t prefix_size;

    if (!output || output_size == 0u)
        return false;
    output[0] = '\0';
    if (!reference || !reference[0])
        return false;
    if (url_has_uri_scheme(reference))
        return url_copy(output, output_size, reference);
    if (!base || !base[0])
        return url_copy(output, output_size, reference);

    if (url_is_remote(base))
    {
        scheme_end = strstr(base, "://");
        authority_start = scheme_end + 3u;
        base_end = strpbrk(authority_start, "?#");
        if (!base_end)
            base_end = base + strlen(base);
        path_start = memchr(authority_start, '/',
                            (size_t)(base_end - authority_start));

        if (reference[0] == '/' && reference[1] == '/')
        {
            prefix_size = (size_t)(scheme_end - base);
            return url_write_parts(output, output_size,
                                   base, prefix_size,
                                   ":", 1u,
                                   reference, strlen(reference));
        }
        if (reference[0] == '/')
        {
            prefix_size = path_start ? (size_t)(path_start - base)
                                     : (size_t)(base_end - base);
            return url_write_parts(output, output_size,
                                   base, prefix_size,
                                   reference, strlen(reference),
                                   NULL, 0u);
        }

        for (const char *cursor = authority_start; cursor < base_end; ++cursor)
        {
            if (*cursor == '/')
                last_slash = cursor;
        }
        prefix_size = last_slash ? (size_t)(last_slash - base)
                                 : (size_t)(base_end - base);
        return url_write_parts(output, output_size,
                               base, prefix_size,
                               "/", 1u,
                               reference, strlen(reference));
    }

    if (reference[0] == '/')
        return url_copy(output, output_size, reference);
    base_end = strpbrk(base, "?#");
    if (!base_end)
        base_end = base + strlen(base);
    for (const char *cursor = base; cursor < base_end; ++cursor)
    {
        if (*cursor == '/')
            last_slash = cursor;
    }
    if (!last_slash)
        return url_copy(output, output_size, reference);
    return url_write_parts(output, output_size,
                           base, (size_t)(last_slash - base),
                           "/", 1u,
                           reference, strlen(reference));
}
