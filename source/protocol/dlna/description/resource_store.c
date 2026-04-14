#include "resource_store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "log/log.h"

#include "dlna_embedded_assets.inc"

typedef struct
{
    const char *name;
    const unsigned char *data;
    size_t size;
} DlnaEmbeddedAsset;

static const DlnaEmbeddedAsset g_assets[] = {
    {"AVTransport.xml", dlna_asset_AVTransport_xml, sizeof(dlna_asset_AVTransport_xml)},
    {"ConnectionManager.xml", dlna_asset_ConnectionManager_xml, sizeof(dlna_asset_ConnectionManager_xml)},
    {"Description.xml", dlna_asset_Description_xml, sizeof(dlna_asset_Description_xml)},
    {"Presentation.html", dlna_asset_Presentation_html, sizeof(dlna_asset_Presentation_html)},
    {"RenderingControl.xml", dlna_asset_RenderingControl_xml, sizeof(dlna_asset_RenderingControl_xml)},
    {"SinkProtocolInfo.csv", dlna_asset_SinkProtocolInfo_csv, sizeof(dlna_asset_SinkProtocolInfo_csv)},
    {"icon.jpg", dlna_asset_icon_jpg, sizeof(dlna_asset_icon_jpg)},
};

static bool ensure_directory(const char *path)
{
    if (!path || path[0] == '\0')
        return false;

    if (mkdir(path, 0777) == 0 || errno == EEXIST)
        return true;

    log_warn("[dlna-assets] mkdir failed path=%s errno=%d\n", path, errno);
    return false;
}

static char *join_path_alloc(const char *dir, const char *name)
{
    int needed;
    char *buffer;

    if (!dir || !name)
        return NULL;

    needed = snprintf(NULL, 0, "%s/%s", dir, name);
    if (needed < 0)
        return NULL;

    buffer = malloc((size_t)needed + 1);
    if (!buffer)
        return NULL;

    snprintf(buffer, (size_t)needed + 1, "%s/%s", dir, name);
    return buffer;
}

static bool ensure_asset_file(const DlnaEmbeddedAsset *asset)
{
    char *path = NULL;
    struct stat st;
    FILE *file = NULL;
    bool ok = false;
    bool needs_write = true;
    unsigned char *existing = NULL;

    if (!asset || !asset->name || !asset->data || asset->size == 0)
        return false;

    path = join_path_alloc(DLNA_STORAGE_DLNA_DIR, asset->name);
    if (!path)
        return false;

    if (stat(path, &st) == 0 && st.st_size > 0)
    {
        if ((size_t)st.st_size == asset->size)
        {
            file = fopen(path, "rb");
            if (file)
            {
                existing = malloc(asset->size);
                if (existing &&
                    fread(existing, 1, asset->size, file) == asset->size &&
                    memcmp(existing, asset->data, asset->size) == 0)
                {
                    needs_write = false;
                }
                free(existing);
                fclose(file);
                file = NULL;
            }
        }
    }

    if (!needs_write)
    {
        free(path);
        return true;
    }

    file = fopen(path, "wb");
    if (!file)
    {
        log_warn("[dlna-assets] fopen failed path=%s errno=%d\n", path, errno);
        free(path);
        return false;
    }

    ok = fwrite(asset->data, 1, asset->size, file) == asset->size;
    fclose(file);

    if (!ok)
        log_warn("[dlna-assets] write failed path=%s\n", path);
    else
        log_info("[dlna-assets] wrote %s\n", path);

    free(path);
    return ok;
}

const char *dlna_resource_store_root(void)
{
    return DLNA_STORAGE_DLNA_DIR;
}

bool dlna_resource_store_ensure_defaults(void)
{
    if (!ensure_directory(DLNA_STORAGE_DIR_PARENT) ||
        !ensure_directory(DLNA_STORAGE_DIR) ||
        !ensure_directory(DLNA_STORAGE_DLNA_DIR))
    {
        return false;
    }

    for (size_t i = 0; i < sizeof(g_assets) / sizeof(g_assets[0]); ++i)
    {
        if (!ensure_asset_file(&g_assets[i]))
            return false;
    }

    return true;
}
