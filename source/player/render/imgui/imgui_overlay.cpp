#include "player/render/imgui_overlay.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>

#include <imgui.h>
#include <switch.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include "iptv/iptv.h"
#include "log/log.h"
#include "player/ui/channel_list.h"
#include "player/ui/home.h"
#include "player/ui/layout.h"
#include "player/ui/overlay.h"
#include "player/ui/utf8.h"
}

extern "C" {
#include "imgui_shaders.inc"
}

namespace
{
constexpr uint32_t kVtxBufferInitialSize = 1024U * 1024U;
constexpr uint32_t kIdxBufferInitialSize = 512U * 1024U;
constexpr uint32_t kImageCount = FRONTEND_DK3D_FRAMEBUFFER_COUNT;
constexpr uint32_t kLogoTextureCount = 12; // Nine rows plus partially visible entries.
constexpr uint32_t kDescriptorCount = 1 + kLogoTextureCount;
constexpr uint32_t kLogoTextureSize = 64;

struct VertUbo
{
    float proj[4][4];
};

struct FragUbo
{
    uint32_t font;
    uint32_t padding[3];
};

struct Buffer
{
    DkMemBlock mem = nullptr;
    uint32_t size = 0;
};

struct LogoTexture
{
    uint32_t channel_id = 0;
    uint64_t last_used = 0;
    bool occupied = false;
    bool valid = false;
    DkMemBlock image_mem = nullptr;
    DkImage image = {};
    DkResHandle handle = 0;
};

bool g_initialized = false;
bool g_failed = false;
bool g_pl_initialized = false;
bool g_context_created = false;
DkDevice g_device = nullptr;
DkQueue g_queue = nullptr;
DkCmdBuf g_upload_cmdbuf = nullptr;
DkMemBlock g_code_mem = nullptr;
DkMemBlock g_ubo_mem = nullptr;
DkMemBlock g_font_image_mem = nullptr;
DkMemBlock g_descriptor_mem = nullptr;
DkImage g_font_image;
DkShader g_shaders[2];
Buffer g_vtx[kImageCount];
Buffer g_idx[kImageCount];
DkResHandle g_font_texture_handle = 0;
LogoTexture g_logo_textures[kLogoTextureCount];
uint64_t g_logo_frame = 0;
bool g_logo_load_attempted = false;

constexpr float kPi = 3.14159265358979323846f;
constexpr ImU32 kPlayerPanel = IM_COL32(12, 16, 23, 208);
constexpr ImU32 kPlayerSurface = IM_COL32(21, 27, 37, 224);
constexpr ImU32 kPlayerBorder = IM_COL32(255, 255, 255, 48);
constexpr ImU32 kPlayerText = IM_COL32(246, 248, 252, 255);
constexpr ImU32 kPlayerMuted = IM_COL32(178, 187, 201, 238);
constexpr ImU32 kPlayerAccent = IM_COL32(0, 185, 212, 255);
constexpr ImU32 kPlayerLive = IM_COL32(255, 91, 81, 255);
constexpr float kPlayerControlRadius = 14.0f;
constexpr float kPlayerTitleSize = 24.0f;
constexpr float kPlayerInfoSize = 18.0f;
constexpr float kPlayerHintSize = 16.0f;

struct SwitchActionHint
{
    const char *button;
    const char *label;
};

void draw_switch_action_hints(ImDrawList *draw,
                              float right,
                              float center_y,
                              const SwitchActionHint *hints,
                              int hint_count,
                              bool dark);
void draw_home_text(ImDrawList *draw, float x, float y, float size, ImU32 color, const char *text);
float text_width(float size, const char *text);
void draw_sized_centered_text(ImDrawList *draw, const char *text, ImVec2 center, float size, ImU32 color);

uint32_t align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

uint32_t align_size(size_t value, uint32_t alignment)
{
    return align_up((uint32_t)value, alignment);
}

DkMemBlock make_memblock(DkDevice device, uint32_t size, uint32_t flags)
{
    DkMemBlockMaker maker;

    dkMemBlockMakerDefaults(&maker, device, align_up(size, DK_MEMBLOCK_ALIGNMENT));
    maker.flags = flags;
    return dkMemBlockCreate(&maker);
}

void destroy_buffer(Buffer &buffer)
{
    if (buffer.mem)
        dkMemBlockDestroy(buffer.mem);
    buffer.mem = nullptr;
    buffer.size = 0;
}

void destroy_resources()
{
    for (uint32_t i = 0; i < kImageCount; ++i)
    {
        destroy_buffer(g_vtx[i]);
        destroy_buffer(g_idx[i]);
    }

    for (uint32_t i = 0; i < kLogoTextureCount; ++i)
    {
        if (g_logo_textures[i].image_mem)
            dkMemBlockDestroy(g_logo_textures[i].image_mem);
        g_logo_textures[i] = {};
    }

    if (g_descriptor_mem)
        dkMemBlockDestroy(g_descriptor_mem);
    if (g_font_image_mem)
        dkMemBlockDestroy(g_font_image_mem);
    if (g_ubo_mem)
        dkMemBlockDestroy(g_ubo_mem);
    if (g_code_mem)
        dkMemBlockDestroy(g_code_mem);

    g_descriptor_mem = nullptr;
    g_font_image_mem = nullptr;
    g_ubo_mem = nullptr;
    g_code_mem = nullptr;
    g_font_texture_handle = 0;
    g_logo_frame = 0;
    g_logo_load_attempted = false;
    memset(&g_font_image, 0, sizeof(g_font_image));
    memset(g_shaders, 0, sizeof(g_shaders));
}

bool load_embedded_shaders(DkDevice device)
{
    const unsigned char *shader_data[2] = {
        nxcast_imgui_vsh_dksh,
        nxcast_imgui_fsh_dksh,
    };
    const unsigned int shader_size[2] = {
        nxcast_imgui_vsh_dksh_len,
        nxcast_imgui_fsh_dksh_len,
    };
    uint32_t offsets[2] = {0, 0};
    uint32_t code_size = DK_SHADER_CODE_UNUSABLE_SIZE;

    for (uint32_t i = 0; i < 2; ++i)
    {
        offsets[i] = code_size;
        code_size = align_up(code_size + shader_size[i], DK_SHADER_CODE_ALIGNMENT);
    }

    g_code_mem = make_memblock(device,
                               code_size,
                               DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    if (!g_code_mem)
        return false;

    auto *dst = static_cast<uint8_t *>(dkMemBlockGetCpuAddr(g_code_mem));
    for (uint32_t i = 0; i < 2; ++i)
    {
        DkShaderMaker maker;

        memcpy(dst + offsets[i], shader_data[i], shader_size[i]);
        dkShaderMakerDefaults(&maker, g_code_mem, offsets[i]);
        dkShaderInitialize(&g_shaders[i], &maker);
        if (!dkShaderIsValid(&g_shaders[i]))
            return false;
    }

    return true;
}

bool load_packaged_font(ImGuiIO &io)
{
    static const char *kFontPath = "sdmc:/switch/NX-Cast/fonts/switch_font.ttf";
    FILE *file = fopen(kFontPath, "rb");
    if (!file)
    {
        log_warn("[player-imgui] packaged font unavailable path=%s; using Switch shared fonts\n",
                 kFontPath);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return false;
    }

    void *data = malloc((size_t)size);
    if (!data)
    {
        fclose(file);
        return false;
    }
    if (fread(data, 1, (size_t)size, file) != (size_t)size)
    {
        free(data);
        fclose(file);
        return false;
    }
    fclose(file);

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = true;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    if (!io.Fonts->AddFontFromMemoryTTF(data,
                                        (int)size,
                                        22.0f,
                                        &cfg,
                                        io.Fonts->GetGlyphRangesChineseFull()))
    {
        free(data);
        return false;
    }

    log_info("[player-imgui] loaded packaged font path=%s bytes=%ld\n", kFontPath, size);
    return true;
}

void load_switch_fonts(ImGuiIO &io)
{
    PlFontData standard;
    PlFontData simplified;
    PlFontData extended_simplified;
    PlFontData extended;
    ImFontConfig cfg;
    ImWchar extended_range[] = {0xe000, 0xe152, 0};

    if (load_packaged_font(io))
        return;

    if (!g_pl_initialized && R_SUCCEEDED(plInitialize(PlServiceType_User)))
        g_pl_initialized = true;

    if (!g_pl_initialized ||
        R_FAILED(plGetSharedFontByType(&standard, PlSharedFontType_Standard)))
    {
        log_warn("[player-imgui] Switch shared font service unavailable; using ImGui default font\n");
        io.Fonts->AddFontDefault();
        return;
    }

    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    io.Fonts->AddFontFromMemoryTTF(standard.address,
                                   standard.size,
                                   22.0f,
                                   &cfg,
                                   io.Fonts->GetGlyphRangesDefault());

    cfg.MergeMode = true;
    if (R_SUCCEEDED(plGetSharedFontByType(
            &simplified, PlSharedFontType_ChineseSimplified)))
    {
        io.Fonts->AddFontFromMemoryTTF(
            simplified.address,
            simplified.size,
            22.0f,
            &cfg,
            io.Fonts->GetGlyphRangesChineseFull());
    }
    if (R_SUCCEEDED(plGetSharedFontByType(
            &extended_simplified, PlSharedFontType_ExtChineseSimplified)))
    {
        io.Fonts->AddFontFromMemoryTTF(
            extended_simplified.address,
            extended_simplified.size,
            22.0f,
            &cfg,
            io.Fonts->GetGlyphRangesChineseFull());
    }
    if (R_SUCCEEDED(plGetSharedFontByType(
            &extended, PlSharedFontType_NintendoExt)))
    {
        io.Fonts->AddFontFromMemoryTTF(extended.address,
                                       extended.size,
                                       22.0f,
                                       &cfg,
                                       extended_range);
    }
    log_info("[player-imgui] loaded Switch shared font fallback\n");
}

bool create_imgui_context()
{
    ImGuiContext *ctx = ImGui::CreateContext();
    if (!ctx)
        return false;

    g_context_created = true;
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendRendererName = "nxcast-imgui-deko3d";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    load_switch_fonts(io);
    io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;

    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 16.0f;
    style.FrameRounding = 12.0f;
    style.GrabRounding = 12.0f;
    style.ScrollbarRounding = 12.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    return true;
}

bool create_font_texture(DkDevice device, DkQueue queue, DkCmdBuf cmdbuf)
{
    ImGuiIO &io = ImGui::GetIO();
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    DkMemBlock upload_mem = nullptr;
    DkImageLayoutMaker layout_maker;
    DkImageLayout layout;
    DkImageView font_view;
    DkCopyBuf copy_src;
    DkImageRect copy_rect;
    DkSampler sampler;
    DkSamplerDescriptor *sampler_desc = nullptr;
    DkImageDescriptor *image_desc = nullptr;

    io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0)
        return false;

    upload_mem = make_memblock(device,
                               (uint32_t)(width * height),
                               DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!upload_mem)
        return false;
    memcpy(dkMemBlockGetCpuAddr(upload_mem), pixels, (size_t)width * (size_t)height);

    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.flags = 0;
    layout_maker.format = DkImageFormat_R8_Unorm;
    layout_maker.dimensions[0] = (uint32_t)width;
    layout_maker.dimensions[1] = (uint32_t)height;
    dkImageLayoutInitialize(&layout, &layout_maker);

    g_font_image_mem = make_memblock(device,
                                     align_up((uint32_t)dkImageLayoutGetSize(&layout),
                                              std::max<uint32_t>(dkImageLayoutGetAlignment(&layout), DK_MEMBLOCK_ALIGNMENT)),
                                     DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!g_font_image_mem)
    {
        dkMemBlockDestroy(upload_mem);
        return false;
    }

    dkImageInitialize(&g_font_image, &layout, g_font_image_mem, 0);
    dkImageViewDefaults(&font_view, &g_font_image);

    g_descriptor_mem = make_memblock(device,
                                     kDescriptorCount * (sizeof(DkSamplerDescriptor) + sizeof(DkImageDescriptor)),
                                     DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!g_descriptor_mem)
    {
        dkMemBlockDestroy(upload_mem);
        return false;
    }

    memset(dkMemBlockGetCpuAddr(g_descriptor_mem),
           0,
           kDescriptorCount * (sizeof(DkSamplerDescriptor) + sizeof(DkImageDescriptor)));
    sampler_desc = static_cast<DkSamplerDescriptor *>(dkMemBlockGetCpuAddr(g_descriptor_mem));
    image_desc = reinterpret_cast<DkImageDescriptor *>(sampler_desc + kDescriptorCount);

    dkSamplerDefaults(&sampler);
    sampler.minFilter = DkFilter_Linear;
    sampler.magFilter = DkFilter_Linear;
    sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
    sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
    sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
    dkSamplerDescriptorInitialize(&sampler_desc[0], &sampler);
    dkImageDescriptorInitialize(&image_desc[0], &font_view, false, false);

    g_font_texture_handle = dkMakeTextureHandle(0, 0);
    io.Fonts->SetTexID((ImTextureID)g_font_texture_handle);

    copy_src.addr = dkMemBlockGetGpuAddr(upload_mem);
    copy_src.rowLength = 0;
    copy_src.imageHeight = 0;
    copy_rect.x = 0;
    copy_rect.y = 0;
    copy_rect.z = 0;
    copy_rect.width = (uint32_t)width;
    copy_rect.height = (uint32_t)height;
    copy_rect.depth = 1;

    dkCmdBufClear(cmdbuf);
    dkCmdBufCopyBufferToImage(cmdbuf, &copy_src, &font_view, &copy_rect, 0);
    dkQueueSubmitCommands(queue, dkCmdBufFinishList(cmdbuf));
    dkQueueWaitIdle(queue);
    dkMemBlockDestroy(upload_mem);
    return true;
}

bool decode_logo_rgba(const char *path, uint8_t **out_pixels)
{
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    const AVCodec *decoder = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    SwsContext *sws = nullptr;
    uint8_t *pixels = nullptr;
    int stream_index = -1;
    bool decoded = false;

    if (!path || !path[0] || !out_pixels)
        return false;
    *out_pixels = nullptr;

    if (avformat_open_input(&format, path, nullptr, nullptr) < 0 ||
        avformat_find_stream_info(format, nullptr) < 0)
        goto done;

    stream_index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (stream_index < 0 || !decoder)
        goto done;

    codec = avcodec_alloc_context3(decoder);
    if (!codec ||
        avcodec_parameters_to_context(codec, format->streams[stream_index]->codecpar) < 0 ||
        avcodec_open2(codec, decoder, nullptr) < 0)
        goto done;

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame)
        goto done;

    while (!decoded && av_read_frame(format, packet) >= 0)
    {
        if (packet->stream_index == stream_index && avcodec_send_packet(codec, packet) >= 0)
            decoded = avcodec_receive_frame(codec, frame) == 0;
        av_packet_unref(packet);
    }
    if (!decoded && avcodec_send_packet(codec, nullptr) >= 0)
        decoded = avcodec_receive_frame(codec, frame) == 0;
    if (!decoded)
        goto done;

    pixels = static_cast<uint8_t *>(malloc(kLogoTextureSize * kLogoTextureSize * 4U));
    if (!pixels)
        goto done;

    sws = sws_getContext(frame->width,
                         frame->height,
                         static_cast<AVPixelFormat>(frame->format),
                         (int)kLogoTextureSize,
                         (int)kLogoTextureSize,
                         AV_PIX_FMT_RGBA,
                         SWS_BILINEAR,
                         nullptr,
                         nullptr,
                         nullptr);
    if (!sws)
        goto done;

    {
        uint8_t *dst_data[4] = {pixels, nullptr, nullptr, nullptr};
        int dst_linesize[4] = {(int)kLogoTextureSize * 4, 0, 0, 0};
        if (sws_scale(sws,
                      frame->data,
                      frame->linesize,
                      0,
                      frame->height,
                      dst_data,
                      dst_linesize) <= 0)
            goto done;
    }

    *out_pixels = pixels;
    pixels = nullptr;

done:
    free(pixels);
    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec);
    if (format)
        avformat_close_input(&format);
    return *out_pixels != nullptr;
}

bool upload_logo_texture(LogoTexture &slot,
                         uint32_t descriptor_index,
                         uint32_t channel_id,
                         const char *path)
{
    uint8_t *pixels = nullptr;
    DkMemBlock upload_mem = nullptr;
    DkMemBlock image_mem = nullptr;
    DkImage image = {};
    DkImageLayoutMaker layout_maker;
    DkImageLayout layout;
    DkImageView image_view;
    DkCopyBuf copy_src = {};
    DkImageRect copy_rect = {};
    bool ok = false;

    if (!g_device || !g_queue || !g_upload_cmdbuf || descriptor_index >= kDescriptorCount)
        return false;
    if (!decode_logo_rgba(path, &pixels))
        goto done;

    upload_mem = make_memblock(g_device,
                               kLogoTextureSize * kLogoTextureSize * 4U,
                               DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!upload_mem)
        goto done;
    memcpy(dkMemBlockGetCpuAddr(upload_mem), pixels, kLogoTextureSize * kLogoTextureSize * 4U);

    dkImageLayoutMakerDefaults(&layout_maker, g_device);
    layout_maker.flags = 0;
    layout_maker.format = DkImageFormat_RGBA8_Unorm;
    layout_maker.dimensions[0] = kLogoTextureSize;
    layout_maker.dimensions[1] = kLogoTextureSize;
    dkImageLayoutInitialize(&layout, &layout_maker);

    image_mem = make_memblock(g_device,
                              align_up((uint32_t)dkImageLayoutGetSize(&layout),
                                       std::max<uint32_t>(dkImageLayoutGetAlignment(&layout), DK_MEMBLOCK_ALIGNMENT)),
                              DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!image_mem)
        goto done;

    dkImageInitialize(&image, &layout, image_mem, 0);
    dkImageViewDefaults(&image_view, &image);
    copy_src.addr = dkMemBlockGetGpuAddr(upload_mem);
    copy_rect.width = kLogoTextureSize;
    copy_rect.height = kLogoTextureSize;
    copy_rect.depth = 1;

    dkQueueWaitIdle(g_queue);
    dkCmdBufClear(g_upload_cmdbuf);
    dkCmdBufCopyBufferToImage(g_upload_cmdbuf, &copy_src, &image_view, &copy_rect, 0);
    dkQueueSubmitCommands(g_queue, dkCmdBufFinishList(g_upload_cmdbuf));
    dkQueueWaitIdle(g_queue);

    {
        auto *sampler_desc = static_cast<DkSamplerDescriptor *>(dkMemBlockGetCpuAddr(g_descriptor_mem));
        auto *image_desc = reinterpret_cast<DkImageDescriptor *>(sampler_desc + kDescriptorCount);
        dkImageDescriptorInitialize(&image_desc[descriptor_index], &image_view, false, false);
    }

    if (slot.image_mem)
        dkMemBlockDestroy(slot.image_mem);
    slot.channel_id = channel_id;
    slot.last_used = g_logo_frame;
    slot.occupied = true;
    slot.valid = true;
    slot.image_mem = image_mem;
    slot.image = image;
    slot.handle = dkMakeTextureHandle(descriptor_index, 0);
    image_mem = nullptr;
    ok = true;

done:
    free(pixels);
    if (image_mem)
        dkMemBlockDestroy(image_mem);
    if (upload_mem)
        dkMemBlockDestroy(upload_mem);
    return ok;
}

ImTextureID channel_logo_texture(const IptvChannel &channel)
{
    LogoTexture *slot = nullptr;
    uint32_t slot_index = 0;

    if (!channel.logo_cached || !channel.logo_path[0])
        return 0;

    for (uint32_t i = 0; i < kLogoTextureCount; ++i)
    {
        if (g_logo_textures[i].occupied && g_logo_textures[i].channel_id == channel.id)
        {
            g_logo_textures[i].last_used = g_logo_frame;
            return g_logo_textures[i].valid ? (ImTextureID)g_logo_textures[i].handle : 0;
        }
    }
    if (g_logo_load_attempted)
        return 0;

    for (uint32_t i = 0; i < kLogoTextureCount; ++i)
    {
        if (!g_logo_textures[i].occupied)
        {
            slot = &g_logo_textures[i];
            slot_index = i;
            break;
        }
        if (!slot || g_logo_textures[i].last_used < slot->last_used)
        {
            slot = &g_logo_textures[i];
            slot_index = i;
        }
    }
    if (!slot)
        return 0;

    g_logo_load_attempted = true;
    if (!upload_logo_texture(*slot, slot_index + 1U, channel.id, channel.logo_path))
    {
        if (slot->image_mem)
        {
            dkQueueWaitIdle(g_queue);
            dkMemBlockDestroy(slot->image_mem);
            slot->image_mem = nullptr;
            slot->image = {};
            slot->handle = 0;
        }
        slot->channel_id = channel.id;
        slot->last_used = g_logo_frame;
        slot->occupied = true;
        slot->valid = false;
        log_warn("[player-imgui] failed to decode IPTV logo path=%s\n", channel.logo_path);
        return 0;
    }
    return (ImTextureID)slot->handle;
}

bool ensure_buffer(DkDevice device, Buffer &buffer, uint32_t needed, uint32_t initial_size)
{
    uint32_t target_size;

    if (buffer.mem && buffer.size >= needed)
        return true;

    destroy_buffer(buffer);
    target_size = std::max(initial_size, align_up(needed + needed / 8U, DK_MEMBLOCK_ALIGNMENT));
    buffer.mem = make_memblock(device,
                               target_size,
                               DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!buffer.mem)
        return false;
    buffer.size = align_up(target_size, DK_MEMBLOCK_ALIGNMENT);
    return true;
}

void setup_render_state(DkCmdBuf cmdbuf, ImDrawData *draw_data, uint32_t width, uint32_t height)
{
    const float left = draw_data->DisplayPos.x;
    const float right = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    const float top = draw_data->DisplayPos.y;
    const float bottom = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    VertUbo vert = {};
    FragUbo frag = {};
    DkViewport viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
    DkRasterizerState rasterizer;
    DkColorState color;
    DkColorWriteState color_write;
    DkBlendState blend;
    DkDepthStencilState depth;
    DkVtxAttribState attrs[3] = {};
    DkVtxBufferState buffers[1] = {};
    const DkShader *shaders[2] = {&g_shaders[0], &g_shaders[1]};
    DkGpuAddr ubo_addr = dkMemBlockGetGpuAddr(g_ubo_mem);
    uint32_t vert_size = align_size(sizeof(VertUbo), DK_UNIFORM_BUF_ALIGNMENT);
    uint32_t frag_size = align_size(sizeof(FragUbo), DK_UNIFORM_BUF_ALIGNMENT);

    vert.proj[0][0] = 2.0f / (right - left);
    vert.proj[1][1] = 2.0f / (top - bottom);
    vert.proj[2][2] = 0.5f;
    vert.proj[3][0] = (right + left) / (left - right);
    vert.proj[3][1] = (top + bottom) / (bottom - top);
    vert.proj[3][2] = 0.5f;
    vert.proj[3][3] = 1.0f;
    frag.font = 1;

    dkCmdBufSetViewports(cmdbuf, 0, &viewport, 1);
    dkCmdBufBindShaders(cmdbuf, DkStageFlag_GraphicsMask, shaders, 2);
    dkCmdBufBindUniformBuffer(cmdbuf, DkStage_Vertex, 0, ubo_addr, vert_size);
    dkCmdBufPushConstants(cmdbuf, ubo_addr, vert_size, 0, sizeof(vert), &vert);
    dkCmdBufBindUniformBuffer(cmdbuf, DkStage_Fragment, 0, ubo_addr + vert_size, frag_size);
    dkCmdBufPushConstants(cmdbuf, ubo_addr + vert_size, frag_size, 0, sizeof(frag), &frag);

    dkRasterizerStateDefaults(&rasterizer);
    rasterizer.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(cmdbuf, &rasterizer);

    dkColorStateDefaults(&color);
    dkColorStateSetBlendEnable(&color, 0, true);
    dkCmdBufBindColorState(cmdbuf, &color);

    dkColorWriteStateDefaults(&color_write);
    dkCmdBufBindColorWriteState(cmdbuf, &color_write);

    dkBlendStateDefaults(&blend);
    dkBlendStateSetFactors(&blend,
                           DkBlendFactor_SrcAlpha,
                           DkBlendFactor_InvSrcAlpha,
                           DkBlendFactor_InvSrcAlpha,
                           DkBlendFactor_Zero);
    dkCmdBufBindBlendState(cmdbuf, 0, &blend);

    dkDepthStencilStateDefaults(&depth);
    depth.depthTestEnable = false;
    depth.depthWriteEnable = false;
    dkCmdBufBindDepthStencilState(cmdbuf, &depth);

    attrs[0].bufferId = 0;
    attrs[0].offset = offsetof(ImDrawVert, pos);
    attrs[0].size = DkVtxAttribSize_2x32;
    attrs[0].type = DkVtxAttribType_Float;
    attrs[1].bufferId = 0;
    attrs[1].offset = offsetof(ImDrawVert, uv);
    attrs[1].size = DkVtxAttribSize_2x32;
    attrs[1].type = DkVtxAttribType_Float;
    attrs[2].bufferId = 0;
    attrs[2].offset = offsetof(ImDrawVert, col);
    attrs[2].size = DkVtxAttribSize_4x8;
    attrs[2].type = DkVtxAttribType_Unorm;
    buffers[0].stride = sizeof(ImDrawVert);
    buffers[0].divisor = 0;
    dkCmdBufBindVtxAttribState(cmdbuf, attrs, 3);
    dkCmdBufBindVtxBufferState(cmdbuf, buffers, 1);
}

void push_fragment_texture_mode(DkCmdBuf cmdbuf, bool font)
{
    FragUbo frag = {};
    const uint32_t vert_size = align_size(sizeof(VertUbo), DK_UNIFORM_BUF_ALIGNMENT);
    const uint32_t frag_size = align_size(sizeof(FragUbo), DK_UNIFORM_BUF_ALIGNMENT);
    const DkGpuAddr frag_addr = dkMemBlockGetGpuAddr(g_ubo_mem) + vert_size;

    frag.font = font ? 1U : 0U;
    dkCmdBufPushConstants(cmdbuf, frag_addr, frag_size, 0, sizeof(frag), &frag);
}


bool text_eq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

bool is_busy_state(PlayerState state)
{
    return state == PLAYER_STATE_LOADING ||
           state == PLAYER_STATE_BUFFERING ||
           state == PLAYER_STATE_SEEKING;
}

bool is_runtime_state(PlayerState state)
{
    return state == PLAYER_STATE_LOADING ||
           state == PLAYER_STATE_BUFFERING ||
           state == PLAYER_STATE_SEEKING ||
           state == PLAYER_STATE_PLAYING ||
           state == PLAYER_STATE_PAUSED;
}

PlayerState display_state_from_context(PlayerState context_state, PlayerState overlay_state)
{
    return is_runtime_state(context_state) ? context_state : overlay_state;
}


void draw_pause_icon(ImDrawList *draw, ImVec2 center, float size, ImU32 color)
{
    const float w = size * 0.18f;
    const float h = size * 0.52f;
    const float gap = size * 0.14f;
    draw->AddRectFilled(ImVec2(center.x - gap - w, center.y - h * 0.5f),
                        ImVec2(center.x - gap, center.y + h * 0.5f),
                        color,
                        w * 0.35f);
    draw->AddRectFilled(ImVec2(center.x + gap, center.y - h * 0.5f),
                        ImVec2(center.x + gap + w, center.y + h * 0.5f),
                        color,
                        w * 0.35f);
}

void draw_play_icon(ImDrawList *draw, ImVec2 center, float size, ImU32 color)
{
    draw->AddTriangleFilled(ImVec2(center.x - size * 0.16f, center.y - size * 0.28f),
                            ImVec2(center.x - size * 0.16f, center.y + size * 0.28f),
                            ImVec2(center.x + size * 0.34f, center.y),
                            color);
}

void draw_seek_icon(ImDrawList *draw, ImVec2 center, int direction, ImU32 color)
{
    const float size = 44.0f;
    const float step = 22.0f * (float)direction;
    for (int i = 0; i < 2; ++i)
    {
        float x = center.x + (float)i * step - step * 0.5f;
        if (direction > 0)
        {
            draw->AddTriangleFilled(ImVec2(x - size * 0.28f, center.y - size * 0.34f),
                                    ImVec2(x - size * 0.28f, center.y + size * 0.34f),
                                    ImVec2(x + size * 0.28f, center.y),
                                    color);
        }
        else
        {
            draw->AddTriangleFilled(ImVec2(x + size * 0.28f, center.y - size * 0.34f),
                                    ImVec2(x + size * 0.28f, center.y + size * 0.34f),
                                    ImVec2(x - size * 0.28f, center.y),
                                    color);
        }
    }
}

void draw_spinner(ImDrawList *draw, ImVec2 center, float radius, ImU32 color)
{
    const float start = (float)ImGui::GetTime() * 3.5f;
    draw->AddCircle(center, radius, IM_COL32(255, 255, 255, 35), 40, 2.5f);
    draw->PathArcTo(center, radius, start, start + kPi * 1.35f, 32);
    draw->PathStroke(color, 0, 2.5f);
}

float switch_key_width(const char *key)
{
    if (strcmp(key, "UP/DN") == 0) return 28;
    if (strcmp(key, "L/R") == 0) return 58;
    return std::max(28.0f, text_width(16, key) + 12);
}

void draw_switch_key(ImDrawList *draw, float x, float y, const char *key, bool dark)
{
    const ImU32 fill = dark ? IM_COL32(242, 245, 248, 245) : IM_COL32(28, 33, 38, 245);
    const ImU32 ink = dark ? IM_COL32(28, 33, 38, 255) : IM_COL32(250, 251, 253, 255);
    if (strcmp(key, "UP/DN") == 0)
    {
        // Direction buttons, not the literal text UP/DN in a capsule.
        draw->AddCircleFilled(ImVec2(x + 14, y - 8), 7, fill, 20);
        draw->AddCircleFilled(ImVec2(x + 14, y + 8), 7, fill, 20);
        draw->AddTriangleFilled(ImVec2(x + 14, y - 11), ImVec2(x + 11, y - 6), ImVec2(x + 17, y - 6), ink);
        draw->AddTriangleFilled(ImVec2(x + 14, y + 11), ImVec2(x + 11, y + 6), ImVec2(x + 17, y + 6), ink);
        return;
    }
    if (strcmp(key, "L/R") == 0)
    {
        for (int i = 0; i < 2; ++i)
        {
            float left = x + i * 32;
            draw->AddRectFilled(ImVec2(left, y - 10), ImVec2(left + 26, y + 10), fill, 5);
            draw_sized_centered_text(draw, i ? "R" : "L", ImVec2(left + 13, y), 14, ink);
        }
        return;
    }
    const float w = switch_key_width(key);
    draw->AddRectFilled(ImVec2(x, y - 14), ImVec2(x + w, y + 14), fill, 14);
    draw_sized_centered_text(draw, key, ImVec2(x + w / 2, y), 16, ink);
}

void draw_video_hint(ImDrawList *draw, float x, float y, float width,
                     const char *key, const char *label)
{
    const float key_width = switch_key_width(key);
    draw_switch_key(draw, x, y, key, true);
    draw->PushClipRect(ImVec2(x + key_width + 7, y - 12), ImVec2(x + width, y + 12), true);
    draw_home_text(draw, x + key_width + 7, y - 8, 16, IM_COL32(214, 223, 228, 220),
                   home_ui_translate(label));
    draw->PopClipRect();
}

void draw_video_action_hints(ImDrawList *draw, float width, float height, bool show_channels, bool seekable)
{
    PlayerUiLayout layout = {};
    if (!player_ui_layout_compute((int)width, (int)height, &layout))
        return;
    const float y = (float)layout.hints_y;
    /* Keep trailing slots aligned with the existing Home/Channels touch zones. */
    const float home_x = width - (show_channels ? 240.0f : 140.0f);
    draw_video_hint(draw, home_x, y, 90, "B", "Home");
    if (show_channels)
        draw_video_hint(draw, width - 140, y, 112, "X", "Channels");
    draw_video_hint(draw, home_x - 180, y, 164, "UP/DN", "Volume");
    if (seekable)
        draw_video_hint(draw, home_x - 294, y, 100, "L/R", "Seek");
    draw_video_hint(draw, home_x - (seekable ? 460 : 346), y, 150, "A", "Play/Pause");
}

void draw_player_busy(ImDrawList *draw, ImVec2 center)
{
    draw->AddCircleFilled(center, 42, IM_COL32(6, 12, 18, 82), 48);
    draw_spinner(draw, center, 18, IM_COL32(230, 249, 249, 238));
}

void draw_center_control(ImDrawList *draw,
                         const PlayerUiOverlayBar &bar,
                         PlayerState display_state,
                         float width,
                         float height)
{
    const ImVec2 center(width * 0.5f, height * 0.5f);
    const ImU32 glass = IM_COL32(6, 12, 18, 102);
    const ImU32 text = IM_COL32(246, 250, 252, 245);
    if (bar.focus == PLAYER_UI_OVERLAY_FOCUS_SEEK)
    {
        char label[128];
        draw->AddRectFilled(ImVec2(center.x - 178, center.y - 56),
                            ImVec2(center.x + 178, center.y + 56), glass, 28);
        if (bar.seek_delta_ms != 0)
        {
            const int seconds = bar.seek_delta_ms / 1000;
            snprintf(label, sizeof(label), "%+d s", seconds);
            draw_seek_icon(draw, ImVec2(center.x - 92, center.y), seconds >= 0 ? 1 : -1, kPlayerAccent);
            draw_sized_centered_text(draw, label, ImVec2(center.x + 24, center.y), 32, text);
        }
        else
        {
            draw_sized_centered_text(draw, bar.center[0] ? bar.center : "--:-- / --:--",
                                     ImVec2(center.x, center.y - 9), 30, text);
            draw_sized_centered_text(draw, home_ui_text("Release to seek", "松手跳转"),
                                     ImVec2(center.x, center.y + 28), 15, kPlayerMuted);
        }
        return;
    }
    if (bar.focus == PLAYER_UI_OVERLAY_FOCUS_VOLUME)
    {
        draw->AddRectFilled(ImVec2(center.x - 120, center.y - 42),
                            ImVec2(center.x + 120, center.y + 42), glass, 28);
        draw_sized_centered_text(draw, bar.right, ImVec2(center.x, center.y - 9), 24, text);
        const float volume = bar.mute ? 0 : std::max(0.0f, std::min(1.0f, bar.volume / 100.0f));
        draw->AddRectFilled(ImVec2(center.x - 78, center.y + 20), ImVec2(center.x + 78, center.y + 23),
                            IM_COL32(255, 255, 255, 55), 2);
        draw->AddRectFilled(ImVec2(center.x - 78, center.y + 20),
                            ImVec2(center.x - 78 + 156 * volume, center.y + 23), kPlayerAccent, 2);
        return;
    }
    if (is_busy_state(display_state))
    {
        draw_player_busy(draw, center);
        return;
    }
    if (display_state != PLAYER_STATE_PAUSED &&
        bar.focus != PLAYER_UI_OVERLAY_FOCUS_PLAY &&
        bar.focus != PLAYER_UI_OVERLAY_FOCUS_PAUSE)
        return;
    draw->AddCircleFilled(center, 42, IM_COL32(6, 12, 18, 82), 48);
    if (display_state == PLAYER_STATE_PAUSED)
        draw_play_icon(draw, ImVec2(center.x - 2, center.y), 48, text);
    else
        draw_pause_icon(draw, center, 48, text);
}

void draw_player_title(ImDrawList *draw, const char *title, float x, float y, float width)
{
    static char previous[sizeof(PlayerUiOverlayBar::subtitle)] = {};
    static double started = 0;
    static double last_draw = 0;
    const double now = armTicksToNs(svcGetSystemTick()) / 1000000000.0;
    if (strcmp(previous, title) != 0 || now - last_draw > 0.35)
    {
        player_utf8_copy_prefix(previous, sizeof(previous), title, strlen(title));
        started = now;
    }
    last_draw = now;
    char single_line[sizeof(previous)];
    memcpy(single_line, previous, sizeof(single_line));
    for (char *p = single_line; *p; ++p)
        if (*p == '\n' || *p == '\r' || *p == '\t')
            *p = ' ';
    const float size = kPlayerTitleSize;
    const float overflow = std::max(0.0f, text_width(size, single_line) - width);
    float offset = 0;
    if (overflow > 0)
    {
        const double hold = 1.0;
        const double travel = overflow / 32.0;
        const double phase = fmod(now - started, 2.0 * (hold + travel));
        if (phase > hold && phase <= hold + travel)
            offset = (float)((phase - hold) * 32.0);
        else if (phase > hold + travel && phase <= 2.0 * hold + travel)
            offset = overflow;
        else if (phase > 2.0 * hold + travel)
            offset = overflow - (float)((phase - 2.0 * hold - travel) * 32.0);
    }
    draw->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + size + 4), true);
    draw_home_text(draw, x - offset, y, size, kPlayerText, single_line);
    draw->PopClipRect();
}

void draw_progress_bar(ImDrawList *draw, const PlayerUiOverlayBar &bar, PlayerState context_state, float width, float height, bool live_tv)
{
    PlayerUiLayout layout;
    if (!player_ui_layout_compute((int)width, (int)height, &layout))
        return;
    const PlayerState display_state = display_state_from_context(context_state, bar.state);
    const float x = (float)layout.progress_x;
    const float w = (float)layout.progress_width;
    const float y = (float)layout.progress_y;
    const bool interactive = !live_tv && bar.seekable && bar.duration_ms > 0;
    const bool preview = bar.focus == PLAYER_UI_OVERLAY_FOCUS_SEEK;
    const float thickness = (float)layout.progress_height + (interactive && preview ? 2.0f : 0.0f);
    const float fill = std::max(0.0f, std::min(1.0f, bar.progress_permille / 1000.0f));
    draw_center_control(draw, bar, display_state, width, height);
    draw->AddRectFilledMultiColor(ImVec2(0, height - layout.bottom_height - 64),
                                  ImVec2(width, height), IM_COL32(3, 8, 12, 0), IM_COL32(3, 8, 12, 0),
                                  IM_COL32(3, 8, 12, 192), IM_COL32(3, 8, 12, 192));
    if (!live_tv && bar.duration_ms > 0)
    {
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + thickness),
                            IM_COL32(255, 255, 255, 65), thickness / 2);
        if (fill > 0)
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w * fill, y + thickness),
                                kPlayerText, thickness / 2);
        if (interactive)
            draw->AddCircleFilled(ImVec2(x + w * fill, y + thickness / 2),
                                  preview ? 7.0f : 4.5f, kPlayerText, 20);
    }
    if (bar.subtitle[0])
        draw_player_title(draw, bar.subtitle, x, (float)layout.title_y, w);
    /* Unknown duration is not necessarily live (e.g. screen mirroring). */
    draw_home_text(draw, x, (float)layout.info_y, kPlayerInfoSize, kPlayerText,
                   live_tv ? home_ui_text("Live", "直播") :
                   bar.duration_ms > 0 ? bar.center : home_ui_text("Streaming", "实时播放"));
    if (bar.right[0])
        draw_home_text(draw, x + w - text_width(kPlayerInfoSize, bar.right), (float)layout.info_y,
                       kPlayerInfoSize, kPlayerMuted, bar.right);
}

void draw_loading_message(ImDrawList *draw, const PlayerUiOverlayMessage &message, PlayerState context_state, float width, float height)
{
    (void)message;
    (void)context_state;
    draw_player_busy(draw, ImVec2(width * 0.5f, height * 0.5f));
}

void draw_message(ImDrawList *draw, const PlayerUiOverlayMessage &message, PlayerState context_state, float width, float height)
{
    ImVec2 center(width * 0.5f, height * 0.5f);
    ImVec2 min(center.x - 190.0f, center.y - 58.0f);
    ImVec2 max(center.x + 190.0f, center.y + 58.0f);

    if (is_busy_state(context_state) ||
        text_eq(message.title, "LOADING") ||
        text_eq(message.title, "BUFFERING") ||
        text_eq(message.title, "SEEKING"))
    {
        draw_loading_message(draw, message, context_state, width, height);
        return;
    }

    draw->AddRectFilled(min, max, IM_COL32(18, 22, 30, 232), 18.0f);
    draw->AddRect(ImVec2(min.x + 1.0f, min.y + 1.0f),
                  ImVec2(max.x - 1.0f, max.y - 1.0f),
                  IM_COL32(82, 92, 112, 180),
                  18.0f,
                  0,
                  2.0f);
    draw->AddText(ImVec2(min.x + 28.0f, min.y + 24.0f),
                  IM_COL32(244, 247, 255, 255),
                  home_ui_translate(message.title));
    if (message.line1[0])
        draw->AddText(ImVec2(min.x + 28.0f, min.y + 58.0f),
                      IM_COL32(168, 176, 190, 255),
                      home_ui_translate(message.line1));
}

void draw_home_text(ImDrawList *draw, float x, float y, float size, ImU32 color, const char *text)
{
    draw->AddText(ImGui::GetFont(), size, ImVec2(x, y), color, text ? text : "");
}

float text_width(float size, const char *text)
{
    if (!text || !text[0])
        return 0.0f;
    return ImGui::GetFont()->CalcTextSizeA(size, 10000.0f, 0.0f, text).x;
}

void draw_sized_centered_text(ImDrawList *draw, const char *text, ImVec2 center, float size, ImU32 color)
{
    const float width = text_width(size, text);
    draw_home_text(draw, center.x - width * 0.5f, center.y - size * 0.5f, size, color, text);
}

void draw_switch_action_hints(ImDrawList *draw,
                              float right,
                              float center_y,
                              const SwitchActionHint *hints,
                              int hint_count,
                              bool dark)
{
    const ImU32 label_text = dark ? IM_COL32(240, 244, 251, 255) : IM_COL32(38, 38, 40, 255);
    const float font_size = kPlayerHintSize;
    float cursor = right;

    for (int i = hint_count - 1; i >= 0; --i)
    {
        const char *label = home_ui_translate(hints[i].label);
        const float label_w = text_width(font_size, label);
        const float button_w = switch_key_width(hints[i].button);
        const float total_w = button_w + 9.0f + label_w;
        const float left = cursor - total_w;

        draw_switch_key(draw, left, center_y, hints[i].button, dark);
        draw_home_text(draw, left + button_w + 9.0f, center_y - 9.0f, font_size, label_text, label);
        cursor = left - 24.0f;
    }
}

ImU32 channel_badge_color(uint32_t id)
{
    static const ImU32 colors[] = {
        IM_COL32(0, 159, 187, 255),
        IM_COL32(236, 82, 73, 255),
        IM_COL32(43, 137, 109, 255),
        IM_COL32(218, 142, 42, 255),
        IM_COL32(55, 100, 174, 255),
    };
    return colors[id % (sizeof(colors) / sizeof(colors[0]))];
}

void format_program_window(time_t start, time_t stop, char *out, size_t out_size)
{
    struct tm start_tm = {};
    struct tm stop_tm = {};

    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (start <= 0 || stop <= 0 || !localtime_r(&start, &start_tm) || !localtime_r(&stop, &stop_tm))
        return;

    snprintf(out,
             out_size,
             "%02d:%02d - %02d:%02d",
             start_tm.tm_hour,
             start_tm.tm_min,
             stop_tm.tm_hour,
             stop_tm.tm_min);
}

float programme_progress(const IptvChannel &channel, time_t now)
{
    if (channel.now_start <= 0 || channel.now_stop <= channel.now_start)
        return 0.0f;

    const double elapsed = difftime(now, channel.now_start);
    const double duration = difftime(channel.now_stop, channel.now_start);
    return (float)std::max(0.0, std::min(1.0, elapsed / duration));
}

void draw_channel_badge(ImDrawList *draw,
                        const IptvChannel &channel,
                        int item_index,
                        ImVec2 min,
                        ImVec2 max)
{
    char number[16];
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const ImTextureID logo = channel_logo_texture(channel);

    snprintf(number, sizeof(number), "%d", item_index + 1);
    if (logo)
    {
        draw->AddRectFilled(min, max, IM_COL32(248, 250, 252, 255), 12.0f);
        draw->AddImageRounded(logo,
                              ImVec2(min.x + 2.0f, min.y + 2.0f),
                              ImVec2(max.x - 2.0f, max.y - 2.0f),
                              ImVec2(0.0f, 0.0f),
                              ImVec2(1.0f, 1.0f),
                              IM_COL32_WHITE,
                              10.0f);
    }
    else
    {
        draw->AddRectFilled(min, max, channel_badge_color(channel.id), 12.0f);
        draw_sized_centered_text(draw, number, center,
                                 std::min(13.0f, 13.0f * (max.x - min.x - 4) / std::max(1.0f, text_width(13, number))),
                                 IM_COL32(255, 255, 255, 255));
    }
}



void draw_home_cast_icon(ImDrawList *draw, ImVec2 center, ImU32 ink, ImU32 accent)
{
    // Match the downloaded Remix Icon cast-fill silhouette with square,
    // disconnected screen bars and an open lower-left corner for cast waves.
    const float x = center.x - 62, y = center.y - 39;
    draw->AddRectFilled(ImVec2(x + 20, y), ImVec2(x + 124, y + 7), ink);
    draw->AddRectFilled(ImVec2(x + 117, y + 4), ImVec2(x + 124, y + 76), ink);
    draw->AddRectFilled(ImVec2(x + 66, y + 69), ImVec2(x + 124, y + 76), ink);
    draw->AddRectFilled(ImVec2(x + 20, y + 4), ImVec2(x + 27, y + 40), ink);
    const ImVec2 origin(x + 20, y + 76);
    for (int i = 0; i < 3; ++i)
    {
        draw->PathArcTo(origin, 12.0f + i * 13.0f, -kPi * 0.5f, 0, 20);
        draw->PathStroke(accent, false, 4.5f);
    }
    draw->AddCircleFilled(origin, 3.5f, accent, 20);
}

void draw_home_tv_icon(ImDrawList *draw, ImVec2 center, ImU32 ink, ImU32 accent)
{
    draw->AddRect(ImVec2(center.x - 64, center.y - 40),
                  ImVec2(center.x + 64, center.y + 40), ink, 8, 0, 4);
    for (int side : {-1, 1})
        draw->AddLine(ImVec2(center.x + side * 38, center.y + 41),
                      ImVec2(center.x + side * 46, center.y + 52), ink, 4);
    draw->AddTriangleFilled(ImVec2(center.x - 9, center.y - 15),
                            ImVec2(center.x - 9, center.y + 15),
                            ImVec2(center.x + 17, center.y), accent);
}

void draw_home_switch(ImDrawList *draw, ImVec2 min, ImVec2 size)
{
    const ImU32 black = IM_COL32(12, 12, 12, 255);
    const ImU32 cyan = IM_COL32(0, 185, 212, 255);
    const ImU32 red = IM_COL32(255, 91, 81, 255);
    const float natural_w = 82.0f;
    const float natural_h = 44.0f;
    const float scale = std::min(size.x / natural_w, size.y / natural_h);
    const float ox = min.x + (size.x - natural_w * scale) * 0.5f;
    const float oy = min.y + (size.y - natural_h * scale) * 0.5f;

    auto p = [&](float x, float y) {
        return ImVec2(ox + x * scale, oy + y * scale);
    };
    auto rr = [&](float value) {
        return value * scale;
    };

    draw->AddRectFilled(p(1.7f, 2.4f),
                        p(14.6f, 41.6f),
                        black,
                        rr(9.0f),
                        ImDrawFlags_RoundCornersLeft);
    draw->AddRectFilled(p(2.5f, 3.1f),
                        p(13.8f, 40.9f),
                        cyan,
                        rr(8.0f),
                        ImDrawFlags_RoundCornersLeft);

    draw->AddRectFilled(p(67.4f, 2.4f),
                        p(80.3f, 41.6f),
                        black,
                        rr(9.0f),
                        ImDrawFlags_RoundCornersRight);
    draw->AddRectFilled(p(68.2f, 3.1f),
                        p(79.5f, 40.9f),
                        red,
                        rr(8.0f),
                        ImDrawFlags_RoundCornersRight);

    draw->AddRectFilled(p(15.6f, 2.4f), p(66.4f, 5.6f), black);
    draw->AddRectFilled(p(15.6f, 2.4f), p(18.8f, 11.6f), black);
    draw->AddRectFilled(p(63.2f, 2.4f), p(66.4f, 41.6f), black);
    draw->AddRectFilled(p(45.0f, 38.4f), p(66.4f, 41.6f), black);
    {
        const ImVec2 origin = p(16.9f, 40.2f);
        const float arc_thickness = rr(3.65f);
        const float radii[] = {rr(5.8f), rr(14.1f), rr(22.4f)};
        for (float radius : radii)
        {
            draw->PathArcTo(origin, radius, -kPi * 0.5f, 0.0f, 28);
            draw->PathStroke(black, false, arc_thickness);
        }
        draw->AddCircleFilled(origin, arc_thickness * 0.56f, black, 18);
    }

    draw->AddCircleFilled(p(5.5f, 23.2f), rr(1.5f), black);
    draw->AddCircleFilled(p(11.6f, 23.2f), rr(1.5f), black);
    draw->AddCircleFilled(p(8.6f, 20.1f), rr(1.5f), black);
    draw->AddCircleFilled(p(8.6f, 12.3f), rr(3.3f), black);
    draw->AddCircleFilled(p(8.6f, 26.3f), rr(1.5f), black);
    draw->AddRectFilled(p(9.5f, 29.4f), p(11.9f, 31.7f), black, rr(0.35f));

    draw->AddCircleFilled(p(79.0f, 12.2f), rr(1.5f), black);
    draw->AddCircleFilled(p(72.9f, 12.2f), rr(1.5f), black);
    draw->AddCircleFilled(p(76.0f, 9.2f), rr(1.5f), black);
    draw->AddCircleFilled(p(76.0f, 23.5f), rr(3.3f), black);
    draw->AddCircleFilled(p(76.0f, 15.3f), rr(1.5f), black);
    draw->AddCircleFilled(p(73.9f, 30.7f), rr(1.6f), black);
}

void draw_home_screen(ImDrawList *draw, const PlayerHomeViewState &home, float width, float height)
{
    const ImU32 ink = IM_COL32(29, 39, 44, 255);
    const ImU32 muted = IM_COL32(96, 108, 113, 255);
    const ImU32 teal = IM_COL32(0, 143, 158, 255);
    const ImU32 border = IM_COL32(215, 220, 218, 255);
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    const float left_center = 336.0f;
    const float right_center = (HOME_TV_LEFT + HOME_TV_RIGHT) * 0.5f;
    auto tr = [](const char *en, const char *zh) { return home_ui_text(en, zh); };
    auto centered = [&](const char *text, float x, float y, float size, ImU32 color) {
        draw_sized_centered_text(draw, text, ImVec2(x, y), size, color);
    };

    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), IM_COL32(244, 243, 238, 255));
    draw->AddRectFilledMultiColor(ImVec2(640, 0), ImVec2(width, 300),
                                 IM_COL32(244, 243, 238, 0), IM_COL32(218, 239, 232, 105),
                                 IM_COL32(244, 243, 238, 0), IM_COL32(244, 243, 238, 0));
    draw_home_switch(draw, ImVec2(48, 38), ImVec2(80, 43));
    draw_home_text(draw, 144, 42, 30, ink, "NX-Cast");
    draw->AddCircleFilled(ImVec2(838, 60), 5,
                         home.network_ready ? IM_COL32(0, 165, 110, 255) : IM_COL32(222, 152, 46, 255));
    draw_home_text(draw, 853, 49, 21, muted,
                   home.network_ready ? tr("Connected", "网络已连接") : tr("Offline", "网络未连接"));
    draw->AddRectFilled(ImVec2(HOME_LANGUAGE_LEFT, HOME_LANGUAGE_TOP),
                        ImVec2(HOME_LANGUAGE_RIGHT, HOME_LANGUAGE_BOTTOM), white, 12);
    draw->AddRect(ImVec2(HOME_LANGUAGE_LEFT, HOME_LANGUAGE_TOP),
                  ImVec2(HOME_LANGUAGE_RIGHT, HOME_LANGUAGE_BOTTOM),
                  home.home_language_focused ? teal : border, 12, 0,
                  home.home_language_focused ? 3.0f : 1.0f);
    centered("中文 / EN", (HOME_LANGUAGE_LEFT + HOME_LANGUAGE_RIGHT) * 0.5f, 60, 23, ink);

    // The receiver is a passive status area, not a selectable card.
    centered(tr("Cast", "手机投屏"), left_center, 177, 34, ink);
    if (home.airplay_pin_visible && home.airplay_pin[0])
    {
        centered(tr("AirPlay pairing", "AirPlay 配对"), left_center, 265, 23, teal);
        centered(home.airplay_pin, left_center, 354, 76, ink);
        centered(tr("Enter this code on your iPhone", "在 iPhone 上输入验证码"),
                 left_center, 432, 22, muted);
    }
    else
    {
        draw_home_cast_icon(draw, ImVec2(left_center, 300), ink, teal);
        const bool ready = home.network_ready && home.video_ready &&
                           (home.dlna_running || home.airplay_running);
        const char *status = ready ? tr("Ready for your phone", "已就绪，等待手机连接")
                                  : tr("Preparing receiver", "正在准备接收服务");
        if (!home.network_ready)
            status = tr("Connect to a network", "请先连接网络");
        if (home.playback_active)
            status = tr("Playback in progress", "正在播放");
        centered(status, left_center, 411, 25, ready ? teal : muted);
        centered(tr("Select on your phone:", "在手机上选择："), left_center, 455, 20, muted);
        centered("NX-Cast", left_center, 493, 30, ink);
        const char *services = home.dlna_running && home.airplay_running ? "DLNA · AirPlay" :
                               home.dlna_running ? "DLNA" :
                               home.airplay_running ? "AirPlay" : tr("Starting services", "正在启动服务");
        centered(services, left_center, 543, 19, muted);
    }

    const bool tv_focused = !home.home_language_focused;
    draw->AddRectFilled(ImVec2(HOME_TV_LEFT, HOME_TV_TOP),
                        ImVec2(HOME_TV_RIGHT, HOME_TV_BOTTOM),
                        tv_focused ? IM_COL32(234, 247, 244, 255) : white, 20);
    draw->AddRect(ImVec2(HOME_TV_LEFT, HOME_TV_TOP),
                  ImVec2(HOME_TV_RIGHT, HOME_TV_BOTTOM),
                  tv_focused ? teal : border, 20, 0, tv_focused ? 3.0f : 1.0f);
    centered(tr("Live TV", "电视直播"), right_center, 177, 34, ink);
    draw_home_tv_icon(draw, ImVec2(right_center, 300), ink, teal);
    char summary[128];
    snprintf(summary, sizeof(summary), tr("%d channels · %d sources", "%d 个频道 · %d 个直播源"),
             home.iptv_channel_count, home.iptv_source_count);
    centered(summary, right_center, 387, 22, muted);
    draw->AddLine(ImVec2(704, 418), ImVec2(1184, 418), border, 1);
    draw_home_text(draw, 704, 438, 18, muted, tr("Recently watched", "最近观看"));
    // Clip user-supplied names by pixels, preserving complete UTF-8 glyphs.
    draw->PushClipRect(ImVec2(704, 466), ImVec2(1184, 507), true);
    draw_home_text(draw, 704, 472, 24, ink,
                   home.iptv_last_name[0] ? home.iptv_last_name : tr("Choose a channel to begin", "选择频道开始观看"));
    draw->PopClipRect();
    draw->AddRectFilled(ImVec2(688, 524), ImVec2(1200, 578), teal, 12);
    draw->AddCircleFilled(ImVec2(722, 551), 15, white);
    centered("X", 722, 551, 19, teal);
    centered(tr("Open channels", "打开频道库"), right_center + 10, 551, 24, white);

    if (home.home_language_save_failed || home.has_error)
    {
        draw->AddRectFilled(ImVec2(48, 610), ImVec2(width - 48, 650),
                            IM_COL32(255, 230, 224, 255), 10);
        draw->PushClipRect(ImVec2(62, 610), ImVec2(width - 62, 650), true);
        draw_home_text(draw, 64, 620, 17, IM_COL32(157, 61, 48, 255),
                       home.home_language_save_failed
                           ? tr("Language changed; could not save to SD card", "语言已切换，但无法保存到 SD 卡")
                           : home.error_line);
        draw->PopClipRect();
    }
    draw->AddLine(ImVec2(48, 662), ImVec2(width - 48, 662), border, 1);
    draw_home_text(draw, 48, 682, 16, muted, "NX-Cast " NXCAST_APP_VERSION);
    const SwitchActionHint hints[] = {
        {"A", home.home_language_focused ? tr("Language", "切换语言") : tr("Open", "打开")},
        {"X", tr("Live TV", "电视直播")},
        {"B", tr("Player", "返回播放")},
        {"+", tr("Exit", "退出")},
    };
    SwitchActionHint visible_hints[4];
    int count = 0;
    for (int i = 0; i < 4; ++i)
        if (i != 2 || home.playback_active)
            visible_hints[count++] = hints[i];
    draw_switch_action_hints(draw, width - 48, 689, visible_hints, count, false);
}

struct BrowserPalette
{
    ImU32 background, surface, text, muted, line, selected, accent, on_accent;
};

BrowserPalette browser_palette(bool dark)
{
    if (dark)
        return {IM_COL32(12, 20, 27, 225), IM_COL32(30, 40, 48, 220),
                IM_COL32(244, 248, 249, 255), IM_COL32(173, 187, 193, 255),
                IM_COL32(255, 255, 255, 24), IM_COL32(19, 74, 77, 245),
                IM_COL32(81, 217, 204, 255), IM_COL32(12, 35, 37, 255)};
    return {IM_COL32(245, 244, 240, 255), IM_COL32(255, 255, 253, 255),
            IM_COL32(24, 42, 44, 255), IM_COL32(101, 117, 119, 255),
            IM_COL32(28, 61, 64, 24), IM_COL32(218, 241, 234, 255),
            IM_COL32(0, 117, 112, 255), IM_COL32(255, 255, 255, 255)};
}

ImVec2 browser_min(PlayerBrowserRect r) { return ImVec2(r.x, r.y); }
ImVec2 browser_max(PlayerBrowserRect r) { return ImVec2(r.x + r.w, r.y + r.h); }

void browser_text(ImDrawList *draw, PlayerBrowserRect r, const char *text, float size, ImU32 color)
{
    if (!text || !text[0] || r.w <= 0) return;
    draw->PushClipRect(browser_min(r), browser_max(r), true);
    draw_home_text(draw, r.x, r.y + (r.h - size) * 0.5f, size, color, text);
    draw->PopClipRect();
}

void browser_toolbar_icon(ImDrawList *draw, ImVec2 c, int icon, ImU32 color)
{
    if (icon == PLAYER_BROWSER_FAVORITES)
    {
        for (int i = 0; i < 10; ++i)
        {
            float angle = -kPi / 2 + i * kPi / 5;
            float radius = i % 2 ? 3.8f : 8.0f;
            draw->PathLineTo(ImVec2(c.x + cosf(angle) * radius, c.y + sinf(angle) * radius));
        }
        draw->PathStroke(color, ImDrawFlags_Closed, 1.6f);
    }
    else if (icon == PLAYER_BROWSER_RECENT)
    {
        draw->AddCircle(c, 7, color, 24, 1.6f);
        draw->AddLine(c, ImVec2(c.x, c.y - 4), color, 1.6f);
        draw->AddLine(c, ImVec2(c.x + 3, c.y + 2), color, 1.6f);
    }
    else if (icon == PLAYER_BROWSER_SEARCH)
    {
        draw->AddCircle(ImVec2(c.x - 2, c.y - 2), 5, color, 24, 1.6f);
        draw->AddLine(ImVec2(c.x + 2, c.y + 2), ImVec2(c.x + 7, c.y + 7), color, 1.8f);
    }
    else if (icon == PLAYER_BROWSER_SOURCE_FILTER)
    {
        for (int i = -1; i <= 1; ++i)
        {
            draw->AddCircleFilled(ImVec2(c.x - 6, c.y + i * 5), 1.3f, color, 8);
            draw->AddLine(ImVec2(c.x - 2, c.y + i * 5), ImVec2(c.x + 7, c.y + i * 5), color, 1.6f);
        }
    }
}

void browser_button(ImDrawList *draw, PlayerBrowserRect r, const char *label,
                    const BrowserPalette &p, bool focused, bool active = false,
                    int icon = -1, const char *key = nullptr)
{
    const bool primary = key && active;
    const ImU32 color = primary ? p.on_accent : active ? p.accent : p.text;
    draw->AddRectFilled(browser_min(r), browser_max(r), primary ? p.accent : active ? p.selected : p.surface, 12);
    draw->AddRect(browser_min(r), browser_max(r), focused ? p.accent : p.line, 12, 0, focused ? 2.5f : 1.0f);
    if (focused && primary)
        draw->AddRect(ImVec2(r.x + 4, r.y + 4), ImVec2(r.x + r.w - 4, r.y + r.h - 4), color, 8, 0, 1);
    const bool dropdown = icon == PLAYER_BROWSER_CATEGORIES || icon == PLAYER_BROWSER_SOURCE_FILTER;
    const bool leading = icon >= 0 && icon != PLAYER_BROWSER_CATEGORIES;
    float size = key ? 19.0f : 17.0f;
    const float key_width = key ? std::max(28.0f, text_width(14, key) + 14) : 0;
    const float prefix = key ? key_width + (label[0] ? 10 : 0) : leading ? 22 : 0;
    const float suffix = dropdown ? 18 : 0;
    while (size > 14 && text_width(size, label) + prefix + suffix > r.w - 12)
        size -= 0.5f;
    const float total = text_width(size, label) + prefix + suffix;
    float x = r.x + (r.w - total) / 2;
    const float cy = r.y + r.h / 2;
    draw->PushClipRect(browser_min(r), browser_max(r), true);
    if (key)
    {
        draw->AddRect(ImVec2(x, cy - 14), ImVec2(x + key_width, cy + 14), color, 14, 0, 1.5f);
        draw_sized_centered_text(draw, key, ImVec2(x + key_width / 2, cy), 14, color);
    }
    else if (leading)
        browser_toolbar_icon(draw, ImVec2(x + 8, cy), icon, color);
    x += prefix;
    draw_sized_centered_text(draw, label, ImVec2(x + text_width(size, label) / 2, cy), size, color);
    if (dropdown)
    {
        const float cx = x + text_width(size, label) + 12;
        draw->PathLineTo(ImVec2(cx - 4, cy - 2));
        draw->PathLineTo(ImVec2(cx, cy + 2));
        draw->PathLineTo(ImVec2(cx + 4, cy - 2));
        draw->PathStroke(color, false, 1.8f);
    }
    draw->PopClipRect();
}

void draw_browser_modal(ImDrawList *draw, const PlayerBrowserView &v, const BrowserPalette &p)
{
    const PlayerBrowserRect modal = player_browser_modal_rect(&v);
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(1280, 720), IM_COL32(0, 0, 0, 145));
    draw->AddRectFilled(browser_min(modal), browser_max(modal), p.background, 24);
    const char *title = v.modal == PLAYER_BROWSER_MODAL_CATEGORIES ? home_ui_text("Categories", "分类") :
                        v.modal == PLAYER_BROWSER_MODAL_SOURCES ? home_ui_text("Playlist sources", "直播源") :
                        home_ui_text("Delete this source?", "删除此直播源？");
    draw_home_text(draw, modal.x + 32, modal.y + 24, 28, p.text, title);
    draw->PushClipRect(ImVec2(192, 180), ImVec2(1088, 516), true);
    const int columns = v.modal == PLAYER_BROWSER_MODAL_CATEGORIES ? 3 : 1;
    const int end = std::min(v.modal_count, v.modal_first_index + columns * 5);
    for (int i = v.modal_first_index; i < end; ++i)
    {
        char name[IPTV_NAME_MAX] = {};
        char detail[80] = {};
        int count = 0;
        bool active = false;
        if (v.modal == PLAYER_BROWSER_MODAL_CATEGORIES)
        {
            if (!iptv_get_filter(i, name, sizeof(name), &count)) continue;
            active = i == iptv_get_filter_index();
            snprintf(detail, sizeof(detail), home_ui_text("%d channels", "%d 个频道"), count);
        }
        else if (v.modal == PLAYER_BROWSER_MODAL_SOURCES)
        {
            if (i == 0)
            {
                snprintf(name, sizeof(name), "%s", home_ui_text("All sources", "全部直播源"));
                active = iptv_get_source_filter() == 0;
            }
            else if (i == 1)
                snprintf(name, sizeof(name), "%s", home_ui_text("Manage sources", "管理直播源"));
            else
            {
                IptvSource source = {};
                if (!iptv_get_source(i - 2, &source)) continue;
                snprintf(name, sizeof(name), "%s", source.name);
                snprintf(detail, sizeof(detail), home_ui_text("%d channels", "%d 个频道"), source.channel_count);
                active = source.id == iptv_get_source_filter();
            }
        }
        else
        {
            snprintf(name, sizeof(name), "%s", i == 0 ? home_ui_text("Cancel", "取消") :
                                                                       home_ui_text("Delete source", "删除直播源"));
            if (i == 1) snprintf(detail, sizeof(detail), "%s", home_ui_text("This cannot be undone", "此操作不可撤销"));
        }
        PlayerBrowserRect r = player_browser_modal_item_rect(&v, i);
        draw->AddRectFilled(browser_min(r), browser_max(r), active ? p.selected : p.surface, 12);
        draw->AddRect(browser_min(r), browser_max(r), i == v.modal_cursor ? p.accent : p.line,
                      12, 0, i == v.modal_cursor ? 2.5f : 1.0f);
        browser_text(draw, {r.x + 16, r.y + 6, r.w - 32, 32}, name, 20, p.text);
        browser_text(draw, {r.x + 16, r.y + 40, r.w - 32, 26}, detail, 16, p.muted);
    }
    draw->PopClipRect();
    const PlayerBrowserRect track = player_browser_scrollbar_rect(&v, false);
    const PlayerBrowserRect thumb = player_browser_scrollbar_rect(&v, true);
    draw->AddRectFilled(browser_min(track), browser_max(track), p.line, 6);
    draw->AddRectFilled(browser_min(thumb), browser_max(thumb), p.accent, 6);
    browser_text(draw, {192, 548, 896, 40},
                 home_ui_text("A / SR  Select     B / SL  Back     Touch outside to close",
                              "A / SR  选择     B / SL  返回     点击面板外关闭"), 17, p.muted);
}

void draw_iptv_panel(ImDrawList *draw, const PlayerHomeViewState &home, float width, float height)
{
    const PlayerBrowserView &v = home.iptv_browser;
    if (v.page == PLAYER_BROWSER_CLOSED) return;
    const bool drawer = v.page == PLAYER_BROWSER_DRAWER;
    const bool sources = v.page == PLAYER_BROWSER_SOURCES;
    const BrowserPalette p = browser_palette(drawer);
    const PlayerBrowserRect panel = player_browser_panel_rect(&v);
    if (drawer)
        draw->AddRectFilledMultiColor(ImVec2(panel.w, 0), ImVec2(panel.w + 100, height),
                                     IM_COL32(0, 0, 0, 65), 0, 0, IM_COL32(0, 0, 0, 65));
    else
        draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), p.background);
    draw->AddRectFilled(browser_min(panel), browser_max(panel), p.background);
    draw_home_text(draw, 24, 32, 30, p.text,
                   sources ? home_ui_text("Playlist sources", "直播源管理") : home_ui_text("Live TV", "电视直播"));

    char subtitle[320] = {};
    if (sources)
        snprintf(subtitle, sizeof(subtitle), home_ui_text("%d sources  /  Local M3U and M3U8 are scanned at startup",
                                                        "%d 个直播源  /  启动时自动扫描本地 M3U、M3U8"), home.iptv_source_count);
    else
        snprintf(subtitle, sizeof(subtitle), "%d / %d   %s%s%s", home.iptv_visible_count,
                 home.iptv_channel_count, home.iptv_active_filter,
                 home.iptv_search[0] ? "  /  " : "", home.iptv_search);
    browser_text(draw, {24, 76, panel.w - 48, 26}, subtitle, 16, p.muted);
    browser_button(draw, player_browser_close_rect(&v), "", p, false, false, -1, "B");
    if (drawer)
        browser_button(draw, player_browser_expand_rect(&v), home_ui_text("X  Full list", "X  全屏列表"), p, false);

    const char *toolbar[] = {
        home_ui_text("Categories", "全部分类"), home_ui_text("Favorites", "收藏"),
        home_ui_text("Recent", "最近观看"), home_ui_text("Search", "搜索"), home_ui_text("Sources", "直播源")};
    const int filter = iptv_get_filter_index();
    for (int i = 0; i < PLAYER_BROWSER_TOOLBAR_COUNT; ++i)
    {
        const bool active = (i == PLAYER_BROWSER_CATEGORIES && filter >= 3) ||
                            (i == PLAYER_BROWSER_FAVORITES && filter == 1) ||
                            (i == PLAYER_BROWSER_RECENT && filter == 2) ||
                            (i == PLAYER_BROWSER_SEARCH && home.iptv_search[0]) ||
                            (i == PLAYER_BROWSER_SOURCE_FILTER && iptv_get_source_filter());
        browser_button(draw, player_browser_toolbar_rect(&v, i), toolbar[i], p,
                       v.focus == PLAYER_BROWSER_FOCUS_TOOLBAR && v.toolbar_focus == i, active, i);
    }

    const PlayerBrowserRect list = player_browser_list_rect(&v);
    draw->PushClipRect(browser_min(list), browser_max(list), true);
    const uint32_t playing_id = home.iptv_playback_active ? iptv_get_playing_channel_id() : 0;
    const time_t now = time(nullptr);
    for (int i = v.first_index; i < v.first_index + v.row_count; ++i)
    {
        PlayerBrowserRect row = player_browser_row_rect(&v, i);
        const bool selected = i == v.selected_index;
        if (selected)
        {
            draw->AddRectFilled(ImVec2(row.x + 2, row.y + 2), ImVec2(row.x + row.w - 2, row.y + row.h - 2),
                                p.selected, 12);
            if (v.focus == PLAYER_BROWSER_FOCUS_ROWS)
                draw->AddRect(ImVec2(row.x + 2, row.y + 2), ImVec2(row.x + row.w - 2, row.y + row.h - 2),
                              p.accent, 12, 0, 2);
        }
        else
            draw->AddLine(ImVec2(row.x + 10, row.y + row.h - 1),
                          ImVec2(row.x + row.w - 10, row.y + row.h - 1), p.line);
        if (sources)
        {
            IptvSource source = {};
            if (!iptv_get_source(i, &source)) continue;
            browser_text(draw, {row.x + 14, row.y + 2, 300, 27}, source.name, 20, p.text);
            browser_text(draw, {row.x + 14, row.y + 29, row.w * 0.60f, 22}, source.url, 14, p.muted);
            browser_text(draw, {row.x + row.w * 0.62f, row.y, row.w * 0.38f - 16, row.h},
                         source.status, 16, p.muted);
        }
        else
        {
            IptvChannel channel = {};
            if (!iptv_get_channel(i, &channel)) continue;
            draw_channel_badge(draw, channel, i, ImVec2(row.x + 10, row.y + 8),
                                ImVec2(row.x + 44, row.y + 42));
            const float name_w = drawer ? row.w - 106 : row.w * 0.34f - 60;
            browser_text(draw, {row.x + 56, row.y + 3, name_w, 26}, channel.name, 20, p.text);
            char program_window[48] = {};
            format_program_window(channel.now_start, channel.now_stop, program_window, sizeof(program_window));
            const float subtitle_w = drawer && program_window[0] ? name_w - 108 : name_w;
            browser_text(draw, {row.x + 56, row.y + 29, subtitle_w, 20},
                         drawer ? (channel.now_title[0] ? channel.now_title : channel.group) :
                                  (channel.group[0] ? channel.group : channel.source), 14, p.muted);
            if (drawer && program_window[0])
            {
                browser_text(draw, {row.x + 56 + subtitle_w + 8, row.y + 29, 100, 20},
                             program_window, 12, p.muted);
                const float x = row.x + 56, w = name_w;
                draw->AddRectFilled(ImVec2(x, row.y + 47), ImVec2(x + w, row.y + 49), p.line, 1);
                draw->AddRectFilled(ImVec2(x, row.y + 47),
                                    ImVec2(x + w * programme_progress(channel, now), row.y + 49), p.accent, 1);
            }
            if (!drawer)
            {
                const float program_x = row.x + row.w * 0.35f;
                browser_text(draw, {program_x, row.y + 2, row.w * 0.41f, 28},
                             channel.now_title[0] ? channel.now_title :
                                 home_ui_text("No programme information", "暂无节目单"), 18, p.text);
                browser_text(draw, {program_x, row.y + 30, row.w * 0.41f, 20},
                             channel.next_title, 14, p.muted);
                char window[48] = {};
                format_program_window(channel.now_start, channel.now_stop, window, sizeof(window));
                browser_text(draw, {row.x + row.w * 0.78f, row.y + 3, row.w * 0.18f, 26}, window, 16, p.muted);
                const float progress = programme_progress(channel, now);
                if (window[0])
                {
                    const float x = row.x + row.w * 0.78f, w = row.w * 0.16f;
                    draw->AddRectFilled(ImVec2(x, row.y + 38), ImVec2(x + w, row.y + 41), p.line, 2);
                    draw->AddRectFilled(ImVec2(x, row.y + 38), ImVec2(x + w * progress, row.y + 41), p.accent, 2);
                }
            }
            if (channel.favorite)
                browser_text(draw, {row.x + row.w - 38, row.y + 3, 24, 22}, "*", 20, p.accent);
            if (playing_id && channel.id == playing_id)
            {
                const float x = row.x + row.w - 30;
                for (int bar = 0; bar < 3; ++bar)
                    draw->AddRectFilled(ImVec2(x + bar * 5, row.y + 39 - (bar == 1 ? 13 : 8)),
                                        ImVec2(x + bar * 5 + 3, row.y + 39), p.accent, 1);
            }
        }
    }
    if (v.item_count == 0)
    {
        browser_text(draw, {list.x + 20, list.y + 130, list.w - 40, 40},
                     home_ui_text("No matching channels", "没有符合条件的频道"), 26, p.text);
        browser_text(draw, {list.x + 20, list.y + 178, list.w - 40, 36},
                     home_ui_text("Change filters, or open Sources to add a playlist.",
                                  "更换筛选条件，或打开直播源添加列表。"), drawer ? 16 : 20, p.muted);
    }
    draw->PopClipRect();
    PlayerBrowserRect track = player_browser_scrollbar_rect(&v, false);
    PlayerBrowserRect thumb = player_browser_scrollbar_rect(&v, true);
    draw->AddRectFilled(browser_min(track), browser_max(track), p.line, 6);
    draw->AddRectFilled(browser_min(thumb), browser_max(thumb), p.accent, 6);
    browser_text(draw, {24, 617, panel.w - 48, 20}, home.iptv_status, 14, p.muted);

    const char *source_actions[] = {
        home_ui_text("Add URL", "添加网址"), home_ui_text("Scan SD", "扫描 SD 卡"),
        home_ui_text("Refresh", "刷新"), home_ui_text("Programme guide", "设置节目单"),
        home_ui_text("Delete", "删除")};
    const char *channel_actions[] = {
        home_ui_text("Play", "播放"), home_ui_text("Favorite", "收藏")};
    for (int i = 0; i < player_browser_action_count(&v); ++i)
        browser_button(draw, player_browser_action_rect(&v, i), sources ? source_actions[i] : channel_actions[i], p,
                       v.focus == PLAYER_BROWSER_FOCUS_ACTIONS && v.action_focus == i, !sources && i == 0,
                       -1, sources ? nullptr : i == 0 ? "A / SR" : "Y");
    if (v.modal != PLAYER_BROWSER_MODAL_NONE)
        draw_browser_modal(draw, v, browser_palette(true));
}

bool render_draw_data(ViewContext *ctx, int slot)
{
    ImDrawData *draw_data = ImGui::GetDrawData();
    uint32_t width;
    uint32_t height;
    DkCmdBuf cmdbuf;
    DkImageView target_view;
    DkGpuAddr descriptor_addr;
    size_t vtx_offset = 0;
    size_t idx_offset = 0;
    DkResHandle bound_texture = UINT32_MAX;

    if (!draw_data || draw_data->CmdListsCount <= 0)
        return false;
    if (slot < 0 || slot >= (int)kImageCount)
        return false;

    width = (uint32_t)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    height = (uint32_t)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (width == 0 || height == 0)
        return false;

    if (!ensure_buffer(ctx->dk3d_device,
                       g_vtx[slot],
                       (uint32_t)(draw_data->TotalVtxCount * sizeof(ImDrawVert)),
                       kVtxBufferInitialSize) ||
        !ensure_buffer(ctx->dk3d_device,
                       g_idx[slot],
                       (uint32_t)(draw_data->TotalIdxCount * sizeof(ImDrawIdx)),
                       kIdxBufferInitialSize))
    {
        return false;
    }

    cmdbuf = ctx->dk3d_overlay_cmdbuf;
    dkCmdBufClear(cmdbuf);
    dkImageViewDefaults(&target_view, &ctx->dk3d_framebuffers[slot]);
    dkCmdBufBindRenderTarget(cmdbuf, &target_view, nullptr);

    descriptor_addr = dkMemBlockGetGpuAddr(g_descriptor_mem);
    dkCmdBufBindSamplerDescriptorSet(cmdbuf, descriptor_addr, kDescriptorCount);
    dkCmdBufBindImageDescriptorSet(cmdbuf,
                                   descriptor_addr + kDescriptorCount * sizeof(DkSamplerDescriptor),
                                   kDescriptorCount);
    dkCmdBufBarrier(cmdbuf, DkBarrier_None, DkInvalidateFlags_Descriptors);
    setup_render_state(cmdbuf, draw_data, width, height);

    dkCmdBufBindVtxBuffer(cmdbuf, 0, dkMemBlockGetGpuAddr(g_vtx[slot].mem), g_vtx[slot].size);
    dkCmdBufBindIdxBuffer(cmdbuf, DkIdxFormat_Uint16, dkMemBlockGetGpuAddr(g_idx[slot].mem));

    for (int n = 0; n < draw_data->CmdListsCount; ++n)
    {
        const ImDrawList *cmd_list = draw_data->CmdLists[n];
        size_t vtx_size = (size_t)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
        size_t idx_size = (size_t)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
        auto *vtx_dst = static_cast<uint8_t *>(dkMemBlockGetCpuAddr(g_vtx[slot].mem)) + vtx_offset;
        auto *idx_dst = static_cast<uint8_t *>(dkMemBlockGetCpuAddr(g_idx[slot].mem)) + idx_offset;

        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, vtx_size);
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, idx_size);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i)
        {
            const ImDrawCmd *cmd = &cmd_list->CmdBuffer[cmd_i];
            ImVec2 clip_min((cmd->ClipRect.x - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x,
                            (cmd->ClipRect.y - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y);
            ImVec2 clip_max((cmd->ClipRect.z - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x,
                            (cmd->ClipRect.w - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y);
            DkScissor scissor;
            DkResHandle texture = (DkResHandle)cmd->GetTexID();

            if (cmd->UserCallback)
                continue;
            if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                continue;
            if (clip_min.x < 0.0f)
                clip_min.x = 0.0f;
            if (clip_min.y < 0.0f)
                clip_min.y = 0.0f;
            if (clip_max.x > (float)width)
                clip_max.x = (float)width;
            if (clip_max.y > (float)height)
                clip_max.y = (float)height;

            scissor.x = (uint32_t)clip_min.x;
            scissor.y = (uint32_t)clip_min.y;
            scissor.width = (uint32_t)(clip_max.x - clip_min.x);
            scissor.height = (uint32_t)(clip_max.y - clip_min.y);
            dkCmdBufSetScissors(cmdbuf, 0, &scissor, 1);

            if (texture != bound_texture)
            {
                bound_texture = texture;
                dkCmdBufBindTexture(cmdbuf, DkStage_Fragment, 0, texture);
                push_fragment_texture_mode(cmdbuf, texture == g_font_texture_handle);
            }

            dkCmdBufDrawIndexed(cmdbuf,
                                DkPrimitive_Triangles,
                                cmd->ElemCount,
                                1,
                                cmd->IdxOffset + (uint32_t)(idx_offset / sizeof(ImDrawIdx)),
                                cmd->VtxOffset + (int32_t)(vtx_offset / sizeof(ImDrawVert)),
                                0);
        }

        vtx_offset += vtx_size;
        idx_offset += idx_size;
    }

    dkQueueSubmitCommands(ctx->dk3d_queue, dkCmdBufFinishList(cmdbuf));
    return true;
}
} // namespace

extern "C" bool frontend_imgui_overlay_init(ViewContext *ctx)
{
    uint32_t ubo_size;

    if (g_initialized)
        return true;
    if (g_failed)
        return false;
    if (!ctx || !ctx->dk3d_device || !ctx->dk3d_queue || !ctx->dk3d_overlay_cmdbuf)
        return false;

    g_device = ctx->dk3d_device;
    g_queue = ctx->dk3d_queue;
    g_upload_cmdbuf = ctx->dk3d_overlay_cmdbuf;
    if (!create_imgui_context())
        goto fail;
    if (!load_embedded_shaders(ctx->dk3d_device))
        goto fail;

    ubo_size = align_size(sizeof(VertUbo), DK_UNIFORM_BUF_ALIGNMENT) +
               align_size(sizeof(FragUbo), DK_UNIFORM_BUF_ALIGNMENT);
    g_ubo_mem = make_memblock(ctx->dk3d_device,
                              ubo_size,
                              DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!g_ubo_mem)
        goto fail;
    if (!create_font_texture(ctx->dk3d_device, ctx->dk3d_queue, ctx->dk3d_overlay_cmdbuf))
        goto fail;

    g_initialized = true;
    log_info("[player-imgui] initialized deko3d overlay\n");
    return true;

fail:
    log_warn("[player-imgui] initialization failed; falling back to C overlay\n");
    frontend_imgui_overlay_shutdown();
    g_failed = true;
    return false;
}

extern "C" void frontend_imgui_overlay_shutdown(void)
{
    destroy_resources();
    if (g_context_created)
    {
        ImGui::DestroyContext();
        g_context_created = false;
    }
    if (g_pl_initialized)
    {
        plExit();
        g_pl_initialized = false;
    }
    g_initialized = false;
    g_device = nullptr;
    g_queue = nullptr;
    g_upload_cmdbuf = nullptr;
}

extern "C" bool frontend_imgui_home_render(ViewContext *ctx, int slot)
{
    ImGuiIO *io;
    ImDrawList *draw;
    PlayerHomeViewState fallback = {};
    const PlayerHomeViewState *home;

    if (!ctx || slot < 0 || slot >= (int)kImageCount)
        return false;
    if (!frontend_imgui_overlay_init(ctx))
        return false;

    home = ctx->home_state_valid ? &ctx->home_state : &fallback;
    io = &ImGui::GetIO();
    io->DisplaySize = ImVec2((float)ctx->status.display_width, (float)ctx->status.display_height);
    io->DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io->DeltaTime = 1.0f / 60.0f;
    io->MouseDown[0] = false;

    ImGui::NewFrame();
    draw = ImGui::GetBackgroundDrawList();
    draw_home_screen(draw, *home, io->DisplaySize.x, io->DisplaySize.y);
    if (home->iptv_panel_open)
        draw_iptv_panel(draw, *home, io->DisplaySize.x, io->DisplaySize.y);
    ImGui::Render();

    if (!render_draw_data(ctx, slot))
        return false;

    ctx->dk3d_overlay_dirty = true;
    return true;
}

extern "C" bool frontend_imgui_overlay_render(ViewContext *ctx, int slot)
{
    PlayerUiOverlaySnapshot overlay = {};
    ImGuiIO *io;
    ImDrawList *draw;
    bool has_player_overlay;
    bool show_iptv_panel;

    if (!ctx || slot < 0 || slot >= (int)kImageCount)
        return false;
    if (!frontend_imgui_overlay_init(ctx))
        return false;
    has_player_overlay = player_ui_overlay_get_snapshot(&overlay) && overlay.kind != PLAYER_UI_OVERLAY_NONE;
    show_iptv_panel = ctx->home_state_valid &&
                      ctx->home_state.iptv_playback_active &&
                      ctx->home_state.iptv_panel_open;
    if (!has_player_overlay && !show_iptv_panel)
    {
        /* Video has already redrawn this target. Empty UI is handled, not a
         * renderer failure: legacy fallback would clear part of this frame. */
        ctx->dk3d_overlay_dirty = false;
        return true;
    }

    io = &ImGui::GetIO();
    io->DisplaySize = ImVec2((float)ctx->status.display_width, (float)ctx->status.display_height);
    io->DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io->DeltaTime = 1.0f / 60.0f;
    io->MouseDown[0] = false;

    ImGui::NewFrame();
    draw = ImGui::GetBackgroundDrawList();
    if (show_iptv_panel)
    {
        draw_iptv_panel(draw, ctx->home_state, io->DisplaySize.x, io->DisplaySize.y);
    }
    else
    {
        if (overlay.kind == PLAYER_UI_OVERLAY_MESSAGE)
            draw_message(draw, overlay.message, ctx->status.player_state, io->DisplaySize.x, io->DisplaySize.y);
        else
            draw_progress_bar(draw, overlay.bar, ctx->status.player_state, io->DisplaySize.x, io->DisplaySize.y,
                              ctx->home_state_valid && ctx->home_state.iptv_playback_active);
        draw_video_action_hints(draw,
                                io->DisplaySize.x,
                                io->DisplaySize.y,
                                ctx->home_state_valid &&
                                    player_iptv_video_menu_available(
                                        ctx->home_state.iptv_playback_active,
                                        ctx->home_state.iptv_channel_count),
                                !ctx->home_state.iptv_playback_active && overlay.kind == PLAYER_UI_OVERLAY_BAR && overlay.bar.seekable && overlay.bar.duration_ms > 0);
    }
    ImGui::Render();

    if (!render_draw_data(ctx, slot))
        return false;

    ctx->dk3d_overlay_dirty = true;
    return true;
}

extern "C" bool frontend_imgui_loading_render(ViewContext *ctx, int slot)
{
    PlayerUiOverlaySnapshot overlay = {};
    PlayerUiOverlayMessage fallback = {};
    ImGuiIO *io;
    ImDrawList *draw;

    if (!ctx || slot < 0 || slot >= (int)kImageCount)
        return false;
    if (!frontend_imgui_overlay_init(ctx))
        return false;

    io = &ImGui::GetIO();
    io->DisplaySize = ImVec2((float)ctx->status.display_width, (float)ctx->status.display_height);
    io->DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io->DeltaTime = 1.0f / 60.0f;
    io->MouseDown[0] = false;

    ImGui::NewFrame();
    draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(ImVec2(0.0f, 0.0f), io->DisplaySize, IM_COL32(4, 6, 10, 255));
    if (ctx->home_state_valid &&
        ctx->home_state.iptv_playback_active &&
        ctx->home_state.iptv_panel_open)
    {
        draw_iptv_panel(draw, ctx->home_state, io->DisplaySize.x, io->DisplaySize.y);
    }
    else if (player_ui_overlay_get_snapshot(&overlay) && overlay.kind == PLAYER_UI_OVERLAY_MESSAGE)
    {
        draw_message(draw, overlay.message, PLAYER_STATE_LOADING, io->DisplaySize.x, io->DisplaySize.y);
    }
    else
    {
        snprintf(fallback.title, sizeof(fallback.title), "LOADING");
        snprintf(fallback.line1, sizeof(fallback.line1), "PREPARING STREAM");
        draw_message(draw, fallback, PLAYER_STATE_LOADING, io->DisplaySize.x, io->DisplaySize.y);
    }
    if (!ctx->home_state_valid ||
        !ctx->home_state.iptv_playback_active ||
        !ctx->home_state.iptv_panel_open)
    {
        draw_video_action_hints(draw,
                                io->DisplaySize.x,
                                io->DisplaySize.y,
                                ctx->home_state_valid &&
                                    player_iptv_video_menu_available(
                                        ctx->home_state.iptv_playback_active,
                                        ctx->home_state.iptv_channel_count), false);
    }
    ImGui::Render();

    if (!render_draw_data(ctx, slot))
        return false;
    ctx->dk3d_overlay_dirty = true;
    return true;
}
