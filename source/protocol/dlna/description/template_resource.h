#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    const char *friendly_name;
    const char *manufacturer;
    const char *manufacturer_url;
    const char *model_description;
    const char *model_name;
    const char *model_number;
    const char *model_url;
    const char *serial_number;
    const char *uuid;
    const char *header_extra;
    const char *service_extra;
} DlnaTemplateValues;

bool dlna_template_render_file_to_buffer(const char *relative_path,
                                         const DlnaTemplateValues *values,
                                         char *out,
                                         size_t out_size,
                                         size_t *out_len);

bool dlna_template_load_file_alloc(const char *relative_path,
                                   char **out,
                                   size_t *out_len);
