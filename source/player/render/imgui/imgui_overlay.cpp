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
#include "player/ui/layout.h"
#include "player/ui/overlay.h"
}

extern "C" {
#include "imgui_shaders.inc"
}

namespace
{
constexpr uint32_t kVtxBufferInitialSize = 1024U * 1024U;
constexpr uint32_t kIdxBufferInitialSize = 512U * 1024U;
constexpr uint32_t kImageCount = FRONTEND_DK3D_FRAMEBUFFER_COUNT;
constexpr uint32_t kLogoTextureCount = PLAYER_IPTV_VISIBLE_ROWS;
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
constexpr float kPlayerPanelRadius = 24.0f;
constexpr float kPlayerControlRadius = 14.0f;
constexpr float kPlayerTitleSize = 22.0f;
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

#if defined(NXCAST_USE_PACKAGED_FONT) && NXCAST_USE_PACKAGED_FONT
bool load_packaged_font(ImGuiIO &io)
{
    static const char *kFontPath = "sdmc:/switch/NX-Cast/fonts/switch_font.ttf";
    FILE *file = fopen(kFontPath, "rb");
    if (!file)
        return false;

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
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    if (!io.Fonts->AddFontFromMemoryTTF(data,
                                        (int)size,
                                        22.0f,
                                        &cfg,
                                        io.Fonts->GetGlyphRangesDefault()))
    {
        free(data);
        return false;
    }

    log_info("[player-imgui] loaded packaged font path=%s bytes=%ld\n", kFontPath, size);
    return true;
}
#endif

void load_switch_fonts(ImGuiIO &io)
{
    PlFontData standard;
    PlFontData extended;
    ImFontConfig cfg;
    ImWchar extended_range[] = {0xe000, 0xe152, 0};

#if defined(NXCAST_USE_PACKAGED_FONT) && NXCAST_USE_PACKAGED_FONT
    if (load_packaged_font(io))
        return;
#endif

    if (!g_pl_initialized && R_SUCCEEDED(plInitialize(PlServiceType_User)))
        g_pl_initialized = true;

    if (!g_pl_initialized ||
        R_FAILED(plGetSharedFontByType(&standard, PlSharedFontType_Standard)) ||
        R_FAILED(plGetSharedFontByType(&extended, PlSharedFontType_NintendoExt)))
    {
        io.Fonts->AddFontDefault();
        return;
    }

    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    io.Fonts->AddFontFromMemoryTTF(standard.address,
                                   standard.size,
                                   22.0f,
                                   &cfg,
                                   io.Fonts->GetGlyphRangesDefault());

    cfg.MergeMode = true;
    io.Fonts->AddFontFromMemoryTTF(extended.address,
                                   extended.size,
                                   22.0f,
                                   &cfg,
                                   extended_range);
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

const char *state_text(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_LOADING:
        return "Loading";
    case PLAYER_STATE_BUFFERING:
        return "Buffering";
    case PLAYER_STATE_SEEKING:
        return "Seeking";
    case PLAYER_STATE_PAUSED:
        return "Paused";
    case PLAYER_STATE_PLAYING:
        return "Playing";
    case PLAYER_STATE_ERROR:
        return "Error";
    case PLAYER_STATE_STOPPED:
        return "Stopped";
    case PLAYER_STATE_IDLE:
    default:
        return "Ready";
    }
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

void draw_centered_text(ImDrawList *draw, const char *text, ImVec2 center, ImU32 color)
{
    ImVec2 size = ImGui::CalcTextSize(text ? text : "");
    draw->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), color, text ? text : "");
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
    const float t = (float)ImGui::GetTime() * 5.0f;
    const int segments = 12;
    for (int i = 0; i < segments; ++i)
    {
        float a = t + ((float)i / (float)segments) * kPi * 2.0f;
        float alpha = (float)(i + 1) / (float)segments;
        ImU32 c = IM_COL32(245, 248, 255, (int)(255.0f * alpha));
        if (color != 0)
            c = IM_COL32((color >> IM_COL32_R_SHIFT) & 0xFF,
                         (color >> IM_COL32_G_SHIFT) & 0xFF,
                         (color >> IM_COL32_B_SHIFT) & 0xFF,
                         (int)(255.0f * alpha));
        ImVec2 p(center.x + cosf(a) * radius, center.y + sinf(a) * radius);
        draw->AddCircleFilled(p, 2.2f + alpha * 2.0f, c);
    }
}

void draw_video_action_hints(ImDrawList *draw, float width, float height, bool show_channels)
{
    PlayerUiLayout layout = {};
    const SwitchActionHint with_channels[] = {
        {"A", "Play/Pause"},
        {"L/R", "Seek"},
        {"UP/DN", "Volume"},
        {"B", "Home"},
        {"X", "Channels"},
    };
    const SwitchActionHint without_channels[] = {
        {"A", "Play/Pause"},
        {"L/R", "Seek"},
        {"UP/DN", "Volume"},
        {"B", "Home"},
    };

    const float hints_y = player_ui_layout_compute((int)width, (int)height, &layout)
                              ? (float)layout.hints_y
                              : height - 40.0f;

    draw_switch_action_hints(draw,
                             width - 36.0f,
                             hints_y,
                             show_channels ? with_channels : without_channels,
                             show_channels ? (int)(sizeof(with_channels) / sizeof(with_channels[0]))
                                           : (int)(sizeof(without_channels) / sizeof(without_channels[0])),
                             true);
}

void draw_center_control(ImDrawList *draw,
                         const PlayerUiOverlayBar &bar,
                         PlayerState display_state,
                         float width,
                         float height)
{
    ImVec2 center(width * 0.5f, height * 0.5f);
    const ImU32 panel = kPlayerPanel;
    const ImU32 ring = kPlayerBorder;
    const ImU32 text = kPlayerText;
    const ImU32 muted = kPlayerMuted;

    if (bar.focus == PLAYER_UI_OVERLAY_FOCUS_SEEK)
    {
        char label[128];

        draw->AddRectFilled(ImVec2(center.x - 154.0f, center.y - 58.0f),
                            ImVec2(center.x + 154.0f, center.y + 58.0f),
                            panel,
                            kPlayerPanelRadius);
        draw->AddRect(ImVec2(center.x - 153.0f, center.y - 57.0f),
                      ImVec2(center.x + 153.0f, center.y + 57.0f),
                      ring,
                      kPlayerPanelRadius,
                      0,
                      2.0f);

        if (bar.seek_delta_ms != 0)
        {
            const int seconds = bar.seek_delta_ms / 1000;
            snprintf(label, sizeof(label), "%+d s", seconds);
            draw_seek_icon(draw, ImVec2(center.x, center.y - 14.0f), seconds >= 0 ? 1 : -1, text);
            draw_centered_text(draw, label, ImVec2(center.x, center.y + 34.0f), text);
        }
        else
        {
            snprintf(label, sizeof(label), "%s", bar.center[0] ? bar.center : "--:-- / --:--");
            draw_centered_text(draw, label, ImVec2(center.x, center.y), text);
            draw_centered_text(draw, "Release to seek", ImVec2(center.x, center.y + 32.0f), muted);
        }
        return;
    }

    if (bar.focus == PLAYER_UI_OVERLAY_FOCUS_VOLUME)
    {
        char label[64];

        snprintf(label, sizeof(label), "%s", bar.right[0] ? bar.right : "VOLUME");
        draw->AddRectFilled(ImVec2(center.x - 124.0f, center.y - 48.0f),
                            ImVec2(center.x + 124.0f, center.y + 48.0f),
                            panel,
                            kPlayerPanelRadius);
        draw_centered_text(draw, label, center, text);
        return;
    }

    if (is_busy_state(display_state))
    {
        draw->AddCircleFilled(center, 48.0f, panel);
        draw->AddCircle(center, 48.0f, ring, 0, 1.5f);
        draw_spinner(draw, center, 21.0f, text);
        draw_sized_centered_text(draw,
                                 state_text(display_state),
                                 ImVec2(center.x, center.y + 64.0f),
                                 kPlayerHintSize,
                                 muted);
        return;
    }

    if (display_state != PLAYER_STATE_PAUSED &&
        bar.focus != PLAYER_UI_OVERLAY_FOCUS_PLAY &&
        bar.focus != PLAYER_UI_OVERLAY_FOCUS_PAUSE)
    {
        return;
    }

    draw->AddCircleFilled(center, 52.0f, panel);
    draw->AddCircle(center, 52.0f, ring, 0, 1.5f);
    if (display_state == PLAYER_STATE_PAUSED || bar.focus == PLAYER_UI_OVERLAY_FOCUS_PLAY)
        draw_play_icon(draw, center, 74.0f, text);
    else
        draw_pause_icon(draw, center, 74.0f, text);
}

void draw_progress_bar(ImDrawList *draw, const PlayerUiOverlayBar &bar, PlayerState context_state, float width, float height)
{
    PlayerUiLayout layout;
    const PlayerState display_state = display_state_from_context(context_state, bar.state);
    if (!player_ui_layout_compute((int)width, (int)height, &layout))
        return;

    const float bottom_h = (float)layout.bottom_height;
    const float progress_x = (float)layout.progress_x;
    const float progress_w = (float)layout.progress_width;
    const float progress_y = (float)layout.progress_y;
    const float progress_h = (float)layout.progress_height;
    const float fill = std::max(0.0f, std::min(1.0f, bar.progress_permille / 1000.0f));
    const ImU32 bg_bottom = IM_COL32(11, 13, 20, 238);
    const ImU32 text = kPlayerText;
    const ImU32 muted = kPlayerMuted;
    const char *center_text = bar.center[0] ? bar.center : state_text(bar.state);
    const char *right_text = bar.right;

    draw_center_control(draw, bar, display_state, width, height);

    draw->AddRectFilledMultiColor(ImVec2(0.0f, height - bottom_h - 36.0f),
                                  ImVec2(width, height),
                                  IM_COL32(2, 4, 9, 0),
                                  IM_COL32(2, 4, 9, 0),
                                  bg_bottom,
                                  bg_bottom);

    draw->AddRectFilled(ImVec2(progress_x, progress_y),
                        ImVec2(progress_x + progress_w, progress_y + progress_h),
                        IM_COL32(68, 74, 88, 255),
                        progress_h * 0.5f);
    draw->AddRectFilled(ImVec2(progress_x, progress_y),
                        ImVec2(progress_x + progress_w * fill, progress_y + progress_h),
                        IM_COL32(245, 248, 255, 255),
                        progress_h * 0.5f);
    draw->AddCircleFilled(ImVec2(progress_x + progress_w * fill, progress_y + progress_h * 0.5f),
                          10.0f,
                          IM_COL32(255, 255, 255, 255));

    if (bar.subtitle[0])
    {
        draw->PushClipRect(ImVec2(progress_x, (float)layout.title_y),
                           ImVec2(progress_x + progress_w, (float)layout.title_y + kPlayerTitleSize + 2.0f),
                           true);
        draw_home_text(draw,
                       progress_x,
                       (float)layout.title_y,
                       kPlayerTitleSize,
                       text,
                       bar.subtitle);
        draw->PopClipRect();
    }

    draw_home_text(draw,
                   progress_x,
                   (float)layout.info_y,
                   kPlayerInfoSize,
                   text,
                   center_text);
    if (right_text[0])
    {
        const float right_width = text_width(kPlayerInfoSize, right_text);
        draw_home_text(draw,
                       progress_x + progress_w - right_width,
                       (float)layout.info_y,
                       kPlayerInfoSize,
                       muted,
                       right_text);
    }
}

void draw_loading_message(ImDrawList *draw, const PlayerUiOverlayMessage &message, PlayerState context_state, float width, float height)
{
    ImVec2 center(width * 0.5f, height * 0.5f);
    const char *label = message.line1[0] ? message.line1 : state_text(context_state);

    draw->AddCircleFilled(center, 50.0f, kPlayerPanel);
    draw->AddCircle(center, 50.0f, kPlayerBorder, 0, 1.5f);
    draw_spinner(draw, center, 22.0f, kPlayerText);
    draw_sized_centered_text(draw,
                             label,
                             ImVec2(center.x, center.y + 66.0f),
                             kPlayerHintSize,
                             kPlayerMuted);
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
                  message.title);
    if (message.line1[0])
        draw->AddText(ImVec2(min.x + 28.0f, min.y + 58.0f),
                      IM_COL32(168, 176, 190, 255),
                      message.line1);
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
    const ImU32 button_bg = dark ? IM_COL32(248, 250, 255, 245) : IM_COL32(19, 20, 23, 245);
    const ImU32 button_text = dark ? IM_COL32(18, 21, 28, 255) : IM_COL32(250, 251, 253, 255);
    const ImU32 label_text = dark ? IM_COL32(240, 244, 251, 255) : IM_COL32(38, 38, 40, 255);
    const float font_size = kPlayerHintSize;
    float cursor = right;

    for (int i = hint_count - 1; i >= 0; --i)
    {
        const float label_w = text_width(font_size, hints[i].label);
        const float button_w = std::max(28.0f, text_width(font_size, hints[i].button) + 14.0f);
        const float total_w = button_w + 9.0f + label_w;
        const float left = cursor - total_w;

        draw->AddRectFilled(ImVec2(left, center_y - 14.0f),
                            ImVec2(left + button_w, center_y + 14.0f),
                            button_bg,
                            kPlayerControlRadius);
        draw_sized_centered_text(draw,
                                 hints[i].button,
                                 ImVec2(left + button_w * 0.5f, center_y - 0.5f),
                                 font_size,
                                 button_text);
        draw_home_text(draw, left + button_w + 9.0f, center_y - 9.0f, font_size, label_text, hints[i].label);
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
    char number[8];
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const ImTextureID logo = channel_logo_texture(channel);

    snprintf(number, sizeof(number), "%02d", (item_index + 1) % 100);
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
        draw_sized_centered_text(draw, number, center, 13.0f, IM_COL32(255, 255, 255, 255));
    }
}

void draw_iptv_channel_drawer(ImDrawList *draw, const PlayerHomeViewState &home, float width, float height)
{
    const float drawer_left = 24.0f;
    const float drawer_right = 786.0f;
    const float drawer_top = 18.0f;
    const float drawer_bottom = height - 18.0f;
    const float list_x = (float)PLAYER_IPTV_LIST_X;
    const float list_w = (float)PLAYER_IPTV_LIST_WIDTH;
    const float rows_y = (float)PLAYER_IPTV_LIST_TOP;
    const float row_h = (float)PLAYER_IPTV_ROW_HEIGHT;
    const int selected_index = home.iptv_selected_index;
    const int first_index = player_iptv_page_start(selected_index);
    const int item_count = home.iptv_visible_count;
    const int page_count = item_count > 0 ? (item_count + PLAYER_IPTV_VISIBLE_ROWS - 1) / PLAYER_IPTV_VISIBLE_ROWS : 0;
    const int current_page = item_count > 0 ? first_index / PLAYER_IPTV_VISIBLE_ROWS + 1 : 0;
    const time_t now = time(nullptr);
    char page_text[32];
    char filter_text[160];

    ++g_logo_frame;
    g_logo_load_attempted = false;

    draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(width, height), IM_COL32(0, 0, 0, 76));
    draw->AddRectFilled(ImVec2(drawer_left + 12.0f, drawer_top + 10.0f),
                        ImVec2(drawer_right + 12.0f, drawer_bottom + 10.0f),
                        IM_COL32(0, 0, 0, 72),
                        kPlayerPanelRadius);
    draw->AddRectFilled(ImVec2(drawer_left, drawer_top),
                        ImVec2(drawer_right, drawer_bottom),
                        IM_COL32(12, 16, 23, 246),
                        kPlayerPanelRadius);
    draw->AddRect(ImVec2(drawer_left, drawer_top),
                  ImVec2(drawer_right, drawer_bottom),
                  kPlayerBorder,
                  kPlayerPanelRadius,
                  0,
                  1.0f);

    draw_home_text(draw, 72.0f, 48.0f, 28.0f, kPlayerText, "Channels");
    draw_home_text(draw, 72.0f, 82.0f, 15.0f, kPlayerMuted, "Live TV and programme guide");

    draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_CHANNEL_TAB_LEFT, (float)PLAYER_IPTV_TAB_TOP),
                        ImVec2((float)PLAYER_IPTV_CHANNEL_TAB_RIGHT, (float)PLAYER_IPTV_TAB_BOTTOM),
                        kPlayerAccent,
                        kPlayerControlRadius);
    draw_sized_centered_text(draw,
                             "Channels",
                             ImVec2((PLAYER_IPTV_CHANNEL_TAB_LEFT + PLAYER_IPTV_CHANNEL_TAB_RIGHT) * 0.5f,
                                    (PLAYER_IPTV_TAB_TOP + PLAYER_IPTV_TAB_BOTTOM) * 0.5f),
                             14.0f,
                             kPlayerText);
    draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_SOURCE_TAB_LEFT, (float)PLAYER_IPTV_TAB_TOP),
                        ImVec2((float)PLAYER_IPTV_SOURCE_TAB_RIGHT, (float)PLAYER_IPTV_TAB_BOTTOM),
                        kPlayerSurface,
                        kPlayerControlRadius);
    draw_sized_centered_text(draw,
                             "Sources",
                             ImVec2((PLAYER_IPTV_SOURCE_TAB_LEFT + PLAYER_IPTV_SOURCE_TAB_RIGHT) * 0.5f,
                                    (PLAYER_IPTV_TAB_TOP + PLAYER_IPTV_TAB_BOTTOM) * 0.5f),
                             14.0f,
                             kPlayerMuted);
    draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_DRAWER_CLOSE_LEFT, (float)PLAYER_IPTV_DRAWER_CLOSE_TOP),
                        ImVec2((float)PLAYER_IPTV_DRAWER_CLOSE_RIGHT, (float)PLAYER_IPTV_DRAWER_CLOSE_BOTTOM),
                        kPlayerSurface,
                        18.0f);
    draw_sized_centered_text(draw,
                             "B",
                             ImVec2((PLAYER_IPTV_DRAWER_CLOSE_LEFT + PLAYER_IPTV_DRAWER_CLOSE_RIGHT) * 0.5f,
                                    (PLAYER_IPTV_DRAWER_CLOSE_TOP + PLAYER_IPTV_DRAWER_CLOSE_BOTTOM) * 0.5f),
                             16.0f,
                             kPlayerText);

    snprintf(filter_text,
             sizeof(filter_text),
             "%.72s%s%.64s",
             home.iptv_active_filter[0] ? home.iptv_active_filter : "All channels",
             home.iptv_search[0] ? "  /  Search: " : "",
             home.iptv_search);
    draw_home_text(draw, list_x, 132.0f, 15.0f, kPlayerAccent, filter_text);
    snprintf(page_text, sizeof(page_text), "%d / %d", current_page, page_count);
    draw_home_text(draw, 566.0f, 132.0f, 14.0f, kPlayerMuted, page_text);

    draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_PAGE_PREV_LEFT, (float)PLAYER_IPTV_PAGE_BUTTON_TOP),
                        ImVec2((float)PLAYER_IPTV_PAGE_PREV_RIGHT, (float)PLAYER_IPTV_PAGE_BUTTON_BOTTOM),
                        current_page > 1 ? kPlayerSurface : IM_COL32(30, 35, 44, 150),
                        12.0f);
    draw_sized_centered_text(draw,
                             "<",
                             ImVec2((PLAYER_IPTV_PAGE_PREV_LEFT + PLAYER_IPTV_PAGE_PREV_RIGHT) * 0.5f,
                                    (PLAYER_IPTV_PAGE_BUTTON_TOP + PLAYER_IPTV_PAGE_BUTTON_BOTTOM) * 0.5f),
                             16.0f,
                             current_page > 1 ? kPlayerText : kPlayerMuted);
    draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_PAGE_NEXT_LEFT, (float)PLAYER_IPTV_PAGE_BUTTON_TOP),
                        ImVec2((float)PLAYER_IPTV_PAGE_NEXT_RIGHT, (float)PLAYER_IPTV_PAGE_BUTTON_BOTTOM),
                        current_page < page_count ? kPlayerSurface : IM_COL32(30, 35, 44, 150),
                        12.0f);
    draw_sized_centered_text(draw,
                             ">",
                             ImVec2((PLAYER_IPTV_PAGE_NEXT_LEFT + PLAYER_IPTV_PAGE_NEXT_RIGHT) * 0.5f,
                                    (PLAYER_IPTV_PAGE_BUTTON_TOP + PLAYER_IPTV_PAGE_BUTTON_BOTTOM) * 0.5f),
                             16.0f,
                             current_page < page_count ? kPlayerText : kPlayerMuted);

    if (item_count <= 0)
    {
        draw_home_text(draw, list_x, rows_y + 38.0f, 24.0f, kPlayerText, "No matching channels");
        draw_home_text(draw, list_x, rows_y + 78.0f, 16.0f, kPlayerMuted, "Change the filter or open Sources to add a playlist.");
    }
    else
    {
        for (int row_index = 0; row_index < PLAYER_IPTV_VISIBLE_ROWS; ++row_index)
        {
            const int item_index = first_index + row_index;
            const float y = rows_y + row_h * row_index;
            IptvChannel channel = {};
            char name[80];
            char programme[112];
            char window[40];
            const bool is_selected = item_index == selected_index;

            if (item_index >= item_count || !iptv_get_channel(item_index, &channel))
                break;

            snprintf(name, sizeof(name), "%.34s", channel.name);
            snprintf(programme,
                     sizeof(programme),
                     "%.48s",
                     channel.now_title[0] ? channel.now_title : "Programme guide unavailable");
            format_program_window(channel.now_start, channel.now_stop, window, sizeof(window));

            draw->AddRectFilled(ImVec2(list_x, y),
                                ImVec2(list_x + list_w, y + row_h - 6.0f),
                                is_selected ? IM_COL32(0, 185, 212, 42) : IM_COL32(255, 255, 255, 8),
                                kPlayerControlRadius);
            if (is_selected)
            {
                draw->AddRect(ImVec2(list_x, y),
                              ImVec2(list_x + list_w, y + row_h - 6.0f),
                              IM_COL32(0, 185, 212, 150),
                              kPlayerControlRadius,
                              0,
                              1.5f);
                draw->AddRectFilled(ImVec2(list_x, y + 12.0f),
                                    ImVec2(list_x + 4.0f, y + row_h - 18.0f),
                                    kPlayerAccent,
                                    2.0f);
            }

            draw_channel_badge(draw,
                               channel,
                               item_index,
                               ImVec2(list_x + 14.0f, y + 7.0f),
                               ImVec2(list_x + 58.0f, y + 51.0f));
            draw_home_text(draw, list_x + 72.0f, y + 5.0f, 19.0f, kPlayerText, name);
            if (channel.favorite)
                draw_home_text(draw, list_x + 454.0f, y + 6.0f, 15.0f, kPlayerAccent, "FAV");
            if (window[0])
            {
                const float window_width = text_width(13.0f, window);
                draw_home_text(draw,
                               list_x + list_w - window_width - 16.0f,
                               y + 8.0f,
                               13.0f,
                               kPlayerMuted,
                               window);
            }
            draw_home_text(draw,
                           list_x + 72.0f,
                           y + 31.0f,
                           14.0f,
                           channel.now_title[0] ? kPlayerMuted : IM_COL32(135, 143, 156, 220),
                           programme);

            draw->AddRectFilled(ImVec2(list_x + 72.0f, y + 53.0f),
                                ImVec2(list_x + list_w - 16.0f, y + 56.0f),
                                IM_COL32(255, 255, 255, 30),
                                1.5f);
            const float progress = programme_progress(channel, now);
            if (progress > 0.0f)
                draw->AddRectFilled(ImVec2(list_x + 72.0f, y + 53.0f),
                                    ImVec2(list_x + 72.0f + (list_w - 88.0f) * progress, y + 56.0f),
                                    kPlayerLive,
                                    1.5f);
        }
    }

    {
        char status[96];
        snprintf(status, sizeof(status), "%.42s", home.iptv_status);
        draw_home_text(draw, list_x, 630.0f, 13.0f, kPlayerMuted, status);
    }
    draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_DRAWER_ACTION_LEFT, (float)PLAYER_IPTV_DRAWER_ACTION_TOP),
                        ImVec2((float)PLAYER_IPTV_DRAWER_ACTION_RIGHT, (float)PLAYER_IPTV_DRAWER_ACTION_BOTTOM),
                        item_count > 0 ? kPlayerAccent : kPlayerSurface,
                        kPlayerControlRadius);
    draw_sized_centered_text(draw,
                             "A  Play channel",
                             ImVec2((PLAYER_IPTV_DRAWER_ACTION_LEFT + PLAYER_IPTV_DRAWER_ACTION_RIGHT) * 0.5f,
                                    (PLAYER_IPTV_DRAWER_ACTION_TOP + PLAYER_IPTV_DRAWER_ACTION_BOTTOM) * 0.5f),
                             14.0f,
                             item_count > 0 ? kPlayerText : kPlayerMuted);
    {
        const SwitchActionHint hints[] = {
            {"Y", "Fav"}, {"L/R", "Page"}, {"ZL/ZR", "Filter"}, {"X", "Sources"}, {"B", "Close"},
        };
        draw_switch_action_hints(draw,
                                 drawer_right - 24.0f,
                                 682.0f,
                                 hints,
                                 (int)(sizeof(hints) / sizeof(hints[0])),
                                 true);
    }
}

const char *home_ready_text(bool ready)
{
    return ready ? "OK" : "WAIT";
}

ImU32 home_status_color(bool ready)
{
    return ready ? IM_COL32(0, 170, 132, 255) : IM_COL32(234, 80, 70, 255);
}

void draw_home_grid(ImDrawList *draw, float width, float height)
{
    const ImU32 bg = IM_COL32(242, 238, 230, 255);
    const ImU32 grid = IM_COL32(222, 218, 210, 84);
    const float step = 42.0f;

    draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(width, height), bg);
    for (float x = 0.0f; x < width; x += step)
        draw->AddLine(ImVec2(x, 0.0f), ImVec2(x, height), grid, 1.0f);
    for (float y = 0.0f; y < height; y += step)
        draw->AddLine(ImVec2(0.0f, y), ImVec2(width, y), grid, 1.0f);

    draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(22.0f, height), IM_COL32(0, 185, 212, 255));
    draw->AddRectFilled(ImVec2(width - 22.0f, 0.0f), ImVec2(width, height), IM_COL32(255, 91, 81, 255));
}

void draw_home_status_chip(ImDrawList *draw, float x, float y, const char *label, bool ready)
{
    const ImU32 panel = IM_COL32(255, 255, 255, 210);
    const ImU32 border = IM_COL32(18, 18, 20, 42);
    const ImU32 text = IM_COL32(18, 18, 20, 255);
    const ImU32 muted = IM_COL32(95, 92, 86, 255);

    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + 176.0f, y + 42.0f), panel, 18.0f);
    draw->AddRect(ImVec2(x, y), ImVec2(x + 176.0f, y + 42.0f), border, 18.0f, 0, 1.0f);
    draw->AddCircleFilled(ImVec2(x + 22.0f, y + 21.0f), 7.0f, home_status_color(ready));
    draw_home_text(draw, x + 38.0f, y + 8.0f, 18.0f, text, label);
    draw_home_text(draw, x + 122.0f, y + 8.0f, 18.0f, muted, home_ready_text(ready));
}

void draw_home_cast_arcs(ImDrawList *draw, ImVec2 origin, float scale, ImU32 color)
{
    const float thickness = 12.0f * scale;
    for (int i = 0; i < 3; ++i)
    {
        float radius = (28.0f + (float)i * 34.0f) * scale;
        draw->PathArcTo(origin, radius, -kPi * 0.5f, 0.0f, 28);
        draw->PathStroke(color, false, thickness);
    }
    draw->AddRectFilled(ImVec2(origin.x - thickness * 0.5f, origin.y - thickness * 0.5f),
                        ImVec2(origin.x + thickness * 0.5f, origin.y + thickness * 0.5f),
                        color,
                        thickness * 0.2f);
}

void draw_home_phone(ImDrawList *draw, ImVec2 min, ImVec2 size, ImU32 color)
{
    draw->AddRect(ImVec2(min.x, min.y),
                  ImVec2(min.x + size.x, min.y + size.y),
                  color,
                  24.0f,
                  0,
                  8.0f);
    draw->AddLine(ImVec2(min.x + size.x * 0.36f, min.y + size.y - 36.0f),
                  ImVec2(min.x + size.x * 0.64f, min.y + size.y - 36.0f),
                  color,
                  7.0f);
}

void draw_home_monitor(ImDrawList *draw, ImVec2 min, ImVec2 size, ImU32 color)
{
    draw->AddRect(ImVec2(min.x, min.y),
                  ImVec2(min.x + size.x, min.y + size.y),
                  color,
                  18.0f,
                  0,
                  8.0f);
    draw->AddRectFilled(ImVec2(min.x + size.x * 0.44f, min.y + size.y),
                        ImVec2(min.x + size.x * 0.56f, min.y + size.y + 30.0f),
                        color);
    draw->AddRectFilled(ImVec2(min.x + size.x * 0.28f, min.y + size.y + 30.0f),
                        ImVec2(min.x + size.x * 0.72f, min.y + size.y + 40.0f),
                        color);
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
    const ImU32 black = IM_COL32(14, 14, 16, 255);
    const ImU32 muted = IM_COL32(78, 76, 70, 255);
    const ImU32 card = IM_COL32(255, 255, 255, 205);
    const ImU32 border = IM_COL32(17, 17, 18, 50);
    const ImU32 cyan = IM_COL32(0, 150, 177, 255);
    const ImU32 white = IM_COL32(250, 252, 253, 255);
    const ImU32 red = IM_COL32(255, 91, 81, 255);
    const float x0 = 72.0f;
    const float top = 56.0f;
    const float cast_right = 796.0f;
    const float iptv_left = 820.0f;
    const float card_top = 278.0f;
    const float card_bottom = 580.0f;
    char error_preview[128];

    if (home.has_error && home.error_line[0])
        snprintf(error_preview, sizeof(error_preview), "%.52s", home.error_line);
    else
        snprintf(error_preview, sizeof(error_preview), "No error recorded.");

    draw_home_grid(draw, width, height);
    draw_home_text(draw, x0, top, 24.0f, black, "NX-CAST / HOME");
    draw_home_text(draw, width - 244.0f, top, 18.0f, muted, "DLNA RECEIVER");
    draw_home_text(draw, x0, top + 70.0f, 54.0f, black, "CAST MEDIA TO YOUR SWITCH");
    draw_home_text(draw, x0, top + 128.0f, 23.0f, muted, "Open a video app, choose NX-Cast, and let the Switch receive the stream.");

    draw_home_status_chip(draw, x0, 220.0f, "Storage", home.storage_ready);
    draw_home_status_chip(draw, x0 + 188.0f, 220.0f, "Network", home.network_ready);
    draw_home_status_chip(draw, x0 + 376.0f, 220.0f, "DLNA", home.dlna_running);
    draw_home_status_chip(draw, x0 + 564.0f, 220.0f, "Player", home.video_ready);

    if (home.has_error && home.error_line[0])
    {
        draw->AddRectFilled(ImVec2(width - 396.0f, 216.0f),
                            ImVec2(width - x0, 264.0f),
                            IM_COL32(255, 230, 226, 238),
                            18.0f);
        draw_home_text(draw, width - 372.0f, 228.0f, 17.0f, red, "Diagnostics");
        draw_home_text(draw, width - 264.0f, 228.0f, 17.0f, muted, "error captured");
    }
    else
    {
        if (home.playback_active)
        {
            char playback[96];
            snprintf(playback, sizeof(playback), "%s  /  A RETURN TO PLAYER", state_text(home.playback_state));
            draw_home_text(draw, width - 390.0f, 232.0f, 18.0f, black, playback);
        }
        else
        {
            draw_home_text(draw, width - 390.0f, 232.0f, 18.0f, muted, "Waiting for a stream.");
        }
    }

    draw->AddRectFilled(ImVec2(x0, card_top), ImVec2(cast_right, card_bottom), card, 30.0f);
    draw->AddRect(ImVec2(x0, card_top), ImVec2(cast_right, card_bottom), border, 30.0f, 0, 1.0f);
    draw_home_text(draw, x0 + 30.0f, card_top + 24.0f, 16.0f, cyan, "CAST RECEIVER");
    draw_home_text(draw, x0 + 30.0f, card_top + 50.0f, 34.0f, black, "READY FOR YOUR PHONE");
    draw_home_text(draw, x0 + 30.0f, card_top + 92.0f, 17.0f, muted, "Select NX-Cast from any DLNA-compatible video app.");

    draw_home_phone(draw, ImVec2(x0 + 44.0f, card_top + 136.0f), ImVec2(66.0f, 112.0f), black);
    draw_home_monitor(draw, ImVec2(x0 + 146.0f, card_top + 149.0f), ImVec2(112.0f, 68.0f), black);
    draw->AddTriangleFilled(ImVec2(x0 + 292.0f, card_top + 182.0f),
                            ImVec2(x0 + 292.0f, card_top + 208.0f),
                            ImVec2(x0 + 320.0f, card_top + 195.0f),
                            red);
    draw_home_cast_arcs(draw, ImVec2(x0 + 390.0f, card_top + 230.0f), 0.63f, black);
    draw->AddTriangleFilled(ImVec2(x0 + 468.0f, card_top + 182.0f),
                            ImVec2(x0 + 468.0f, card_top + 208.0f),
                            ImVec2(x0 + 496.0f, card_top + 195.0f),
                            red);
    draw_home_switch(draw, ImVec2(x0 + 520.0f, card_top + 143.0f), ImVec2(158.0f, 106.0f));
    draw_home_text(draw, x0 + 44.0f, card_top + 262.0f, 15.0f, muted, "PHONE / PC");
    draw_home_text(draw, x0 + 366.0f, card_top + 262.0f, 15.0f, muted, "DLNA");
    draw_home_text(draw, x0 + 565.0f, card_top + 262.0f, 15.0f, muted, "NX-CAST");

    draw->AddRectFilled(ImVec2(iptv_left, card_top), ImVec2(width - x0, card_bottom), cyan, 30.0f);
    draw->AddRectFilled(ImVec2(iptv_left, card_top), ImVec2(iptv_left + 10.0f, card_bottom), red, 5.0f);
    draw_home_text(draw, iptv_left + 32.0f, card_top + 26.0f, 16.0f, IM_COL32(226, 249, 252, 230), "WATCH CHANNELS");
    draw_home_monitor(draw, ImVec2(iptv_left + 34.0f, card_top + 72.0f), ImVec2(96.0f, 60.0f), white);
    draw->AddTriangleFilled(ImVec2(iptv_left + 72.0f, card_top + 88.0f),
                            ImVec2(iptv_left + 72.0f, card_top + 116.0f),
                            ImVec2(iptv_left + 96.0f, card_top + 102.0f),
                            red);
    draw_home_text(draw, iptv_left + 154.0f, card_top + 61.0f, 48.0f, white, "IPTV");
    draw_home_text(draw, iptv_left + 154.0f, card_top + 113.0f, 18.0f, IM_COL32(226, 249, 252, 230), "CHANNEL LIBRARY");
    {
        char summary[160];
        char status[128];

        snprintf(summary,
                 sizeof(summary),
                 "%d channel%s from %d source%s",
                 home.iptv_channel_count,
                 home.iptv_channel_count == 1 ? "" : "s",
                 home.iptv_source_count,
                 home.iptv_source_count == 1 ? "" : "s");
        snprintf(status, sizeof(status), "%.52s", home.iptv_status[0] ? home.iptv_status : "IPTV is not initialized.");
        draw_home_text(draw, iptv_left + 32.0f, card_top + 166.0f, 20.0f, white, summary);
        draw_home_text(draw, iptv_left + 32.0f, card_top + 198.0f, 15.0f, IM_COL32(226, 249, 252, 220), status);
    }
    draw->AddRectFilled(ImVec2(iptv_left + 32.0f, card_top + 236.0f),
                        ImVec2(width - x0 - 30.0f, card_top + 278.0f),
                        IM_COL32(255, 255, 255, 232),
                        18.0f);
    draw->AddCircleFilled(ImVec2(iptv_left + 58.0f, card_top + 257.0f), 14.0f, black);
    draw_sized_centered_text(draw, "X", ImVec2(iptv_left + 58.0f, card_top + 256.0f), 15.0f, white);
    draw_home_text(draw, iptv_left + 82.0f, card_top + 246.0f, 18.0f, black, "OPEN CHANNEL LIBRARY");

    draw->AddLine(ImVec2(x0, 620.0f), ImVec2(width - x0, 620.0f), IM_COL32(17, 17, 18, 42), 1.0f);
    draw_home_text(draw, x0, 644.0f, 15.0f, home.has_error ? red : cyan, home.has_error ? "DIAGNOSTICS" : "SYSTEM READY");
    draw_home_text(draw,
                   x0,
                   668.0f,
                   15.0f,
                   home.has_error ? red : muted,
                   home.has_error ? error_preview : "Waiting for DLNA or IPTV playback.");
    if (home.playback_active)
    {
        const SwitchActionHint hints[] = {
            {"A", "Player"}, {"X", "IPTV"}, {"-", "Open URL"}, {"Y", "Refresh"}, {"+", "Exit"},
        };
        draw_switch_action_hints(draw,
                                 width - x0,
                                 663.0f,
                                 hints,
                                 (int)(sizeof(hints) / sizeof(hints[0])),
                                 false);
    }
    else
    {
        const SwitchActionHint hints[] = {
            {"X", "IPTV"}, {"-", "Open URL"}, {"Y", "Refresh"}, {"+", "Exit"},
        };
        draw_switch_action_hints(draw,
                                 width - x0,
                                 663.0f,
                                 hints,
                                 (int)(sizeof(hints) / sizeof(hints[0])),
                                 false);
    }
}

void draw_iptv_panel(ImDrawList *draw, const PlayerHomeViewState &home, float width, float height)
{
    const ImU32 black = kPlayerText;
    const ImU32 muted = kPlayerMuted;
    const ImU32 panel = kPlayerPanel;
    const ImU32 row = kPlayerSurface;
    const ImU32 selected = IM_COL32(0, 185, 212, 42);
    const ImU32 border = kPlayerBorder;
    const ImU32 cyan = kPlayerAccent;
    const ImU32 red = kPlayerLive;
    const float margin = 46.0f;
    const float list_x = (float)PLAYER_IPTV_LIST_X;
    const float list_w = (float)PLAYER_IPTV_LIST_WIDTH;
    const float details_x = 786.0f;
    const float details_w = width - details_x - 72.0f;
    const float rows_y = (float)PLAYER_IPTV_LIST_TOP;
    const float row_h = (float)PLAYER_IPTV_ROW_HEIGHT;
    const int visible_rows = PLAYER_IPTV_VISIBLE_ROWS;
    int selected_index = home.iptv_sources_open ? home.iptv_source_selected_index : home.iptv_selected_index;
    int first_index = player_iptv_page_start(selected_index);
    const int item_count = home.iptv_sources_open ? home.iptv_source_count : home.iptv_visible_count;
    const int page_count = item_count > 0 ? (item_count + visible_rows - 1) / visible_rows : 0;
    const int current_page = item_count > 0 ? first_index / visible_rows + 1 : 0;
    IptvChannel selected_channel = {};
    IptvSource selected_source = {};
    bool have_selected_channel = !home.iptv_sources_open && iptv_get_channel(selected_index, &selected_channel);
    bool have_selected_source = home.iptv_sources_open && iptv_get_source(selected_index, &selected_source);

    if (!home.iptv_sources_open)
    {
        draw_iptv_channel_drawer(draw, home, width, height);
        return;
    }

    draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(width, height), IM_COL32(0, 0, 0, 76));
    draw->AddRectFilled(ImVec2(margin, 38.0f), ImVec2(width - margin, height - 30.0f), panel, kPlayerPanelRadius);
    draw->AddRect(ImVec2(margin, 38.0f), ImVec2(width - margin, height - 30.0f), border, kPlayerPanelRadius, 0, 1.0f);

    draw_home_text(draw, 72.0f, 64.0f, 27.0f, black, "IPTV");
    draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_CHANNEL_TAB_LEFT, (float)PLAYER_IPTV_TAB_TOP),
                        ImVec2((float)PLAYER_IPTV_CHANNEL_TAB_RIGHT, (float)PLAYER_IPTV_TAB_BOTTOM),
                        home.iptv_sources_open ? kPlayerSurface : cyan,
                        kPlayerControlRadius);
    draw_sized_centered_text(draw,
                             "Channels",
                             ImVec2((PLAYER_IPTV_CHANNEL_TAB_LEFT + PLAYER_IPTV_CHANNEL_TAB_RIGHT) * 0.5f,
                                    (PLAYER_IPTV_TAB_TOP + PLAYER_IPTV_TAB_BOTTOM) * 0.5f),
                             13.0f,
                             home.iptv_sources_open ? muted : IM_COL32(255, 255, 255, 255));
    draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_SOURCE_TAB_LEFT, (float)PLAYER_IPTV_TAB_TOP),
                        ImVec2((float)PLAYER_IPTV_SOURCE_TAB_RIGHT, (float)PLAYER_IPTV_TAB_BOTTOM),
                        home.iptv_sources_open ? cyan : kPlayerSurface,
                        kPlayerControlRadius);
    draw_sized_centered_text(draw,
                             "Sources",
                             ImVec2((PLAYER_IPTV_SOURCE_TAB_LEFT + PLAYER_IPTV_SOURCE_TAB_RIGHT) * 0.5f,
                                    (PLAYER_IPTV_TAB_TOP + PLAYER_IPTV_TAB_BOTTOM) * 0.5f),
                             13.0f,
                             home.iptv_sources_open ? IM_COL32(255, 255, 255, 255) : muted);
    draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_CLOSE_LEFT, (float)PLAYER_IPTV_CLOSE_TOP),
                        ImVec2((float)PLAYER_IPTV_CLOSE_RIGHT, (float)PLAYER_IPTV_CLOSE_BOTTOM),
                        kPlayerSurface,
                        18.0f);
    draw_sized_centered_text(draw,
                             "X",
                             ImVec2((PLAYER_IPTV_CLOSE_LEFT + PLAYER_IPTV_CLOSE_RIGHT) * 0.5f,
                                    (PLAYER_IPTV_CLOSE_TOP + PLAYER_IPTV_CLOSE_BOTTOM) * 0.5f),
                             17.0f,
                             muted);
    draw_home_text(draw,
                   72.0f,
                   104.0f,
                   18.0f,
                   muted,
                   home.iptv_sources_open ? "Choose a playlist, then connect its guide.xml when no guide was found automatically" : "Browse, filter, favorite, and play your channel library");
    {
        char count[128];
        if (home.iptv_sources_open)
            snprintf(count, sizeof(count), "%d sources%s", home.iptv_source_count, home.iptv_refreshing ? "  /  Refreshing" : "");
        else
            snprintf(count,
                     sizeof(count),
                     "%d shown  /  %d total  /  %d fav",
                     home.iptv_visible_count,
                     home.iptv_channel_count,
                     home.iptv_favorite_count);
        draw_home_text(draw, width - 570.0f, 72.0f, 18.0f, cyan, count);
    }

    if (!home.iptv_sources_open)
    {
        char filter[192];
        snprintf(filter,
                 sizeof(filter),
                 "FILTER  %.28s%s%.22s",
                 home.iptv_active_filter[0] ? home.iptv_active_filter : "All channels",
                 home.iptv_search[0] ? "     SEARCH  " : "",
                 home.iptv_search);
        draw_home_text(draw, 72.0f, 136.0f, 15.0f, cyan, filter);
    }

    {
        char page_text[32];
        snprintf(page_text, sizeof(page_text), "%d / %d", current_page, page_count);
        draw_home_text(draw, 560.0f, 136.0f, 14.0f, muted, page_text);
        draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_PAGE_PREV_LEFT, (float)PLAYER_IPTV_PAGE_BUTTON_TOP),
                            ImVec2((float)PLAYER_IPTV_PAGE_PREV_RIGHT, (float)PLAYER_IPTV_PAGE_BUTTON_BOTTOM),
                            current_page > 1 ? cyan : IM_COL32(30, 35, 44, 150),
                            12.0f);
        draw_sized_centered_text(draw,
                                 "<",
                                 ImVec2((PLAYER_IPTV_PAGE_PREV_LEFT + PLAYER_IPTV_PAGE_PREV_RIGHT) * 0.5f,
                                        (PLAYER_IPTV_PAGE_BUTTON_TOP + PLAYER_IPTV_PAGE_BUTTON_BOTTOM) * 0.5f),
                                 16.0f,
                                 current_page > 1 ? IM_COL32(255, 255, 255, 255) : muted);
        draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_PAGE_NEXT_LEFT, (float)PLAYER_IPTV_PAGE_BUTTON_TOP),
                            ImVec2((float)PLAYER_IPTV_PAGE_NEXT_RIGHT, (float)PLAYER_IPTV_PAGE_BUTTON_BOTTOM),
                            current_page < page_count ? cyan : IM_COL32(30, 35, 44, 150),
                            12.0f);
        draw_sized_centered_text(draw,
                                 ">",
                                 ImVec2((PLAYER_IPTV_PAGE_NEXT_LEFT + PLAYER_IPTV_PAGE_NEXT_RIGHT) * 0.5f,
                                        (PLAYER_IPTV_PAGE_BUTTON_TOP + PLAYER_IPTV_PAGE_BUTTON_BOTTOM) * 0.5f),
                                 16.0f,
                                 current_page < page_count ? IM_COL32(255, 255, 255, 255) : muted);
    }

    draw->AddRectFilled(ImVec2(list_x, rows_y - 12.0f),
                        ImVec2(list_x + list_w, rows_y + row_h * visible_rows + 12.0f),
                        IM_COL32(18, 23, 32, 232),
                        kPlayerControlRadius);

    if (item_count <= 0)
    {
        draw_home_text(draw,
                       list_x + 28.0f,
                       rows_y + 36.0f,
                       24.0f,
                       black,
                       home.iptv_sources_open ? "No IPTV sources" : "No matching channels");
        if (home.iptv_sources_open)
        {
            draw_home_text(draw, list_x + 28.0f, rows_y + 78.0f, 18.0f, muted, "Press Y to import an M3U playlist address.");
            draw_home_text(draw, list_x + 28.0f, rows_y + 112.0f, 18.0f, muted, "Long URLs can be preloaded from:");
            draw_home_text(draw, list_x + 28.0f, rows_y + 144.0f, 18.0f, cyan, IPTV_PREINSTALLED_SOURCES_FILE);
        }
        else
        {
            draw_home_text(draw, list_x + 28.0f, rows_y + 78.0f, 18.0f, muted, "Change the group filter or clear search with R3.");
            draw_home_text(draw, list_x + 28.0f, rows_y + 112.0f, 18.0f, muted, "Press X to manage playlist sources.");
        }
    }
    else
    {
        for (int row_index = 0; row_index < visible_rows; ++row_index)
        {
            int item_index = first_index + row_index;
            float y = rows_y + row_h * row_index;
            char index_text[16];
            char name_text[80];
            char secondary_text[112];
            char group_text[64];
            bool favorite = false;
            bool has_program = false;
            bool source_has_guide = false;
            bool source_local = false;
            uint32_t badge_id = (uint32_t)item_index;

            secondary_text[0] = '\0';
            group_text[0] = '\0';

            if (home.iptv_sources_open)
            {
                IptvSource source = {};
                if (!iptv_get_source(item_index, &source))
                    break;
                snprintf(name_text, sizeof(name_text), "%.34s", source.name);
                snprintf(secondary_text,
                         sizeof(secondary_text),
                         "%s  /  %d channels%s",
                         source.local ? "Local" : (source.cache_ready ? "Cached" : "Remote"),
                         source.channel_count,
                         source.refreshing ? "  /  Syncing" : "");
                source_has_guide = source.epg_url[0] != '\0';
                source_local = source.local;
                badge_id = source.id;
            }
            else
            {
                IptvChannel channel = {};
                if (!iptv_get_channel(item_index, &channel))
                    break;
                snprintf(name_text, sizeof(name_text), "%.28s", channel.name);
                snprintf(group_text, sizeof(group_text), "%.18s", channel.group[0] ? channel.group : "Ungrouped");
                snprintf(secondary_text,
                         sizeof(secondary_text),
                         "%.48s",
                         channel.now_title[0] ? channel.now_title : "Programme guide unavailable");
                favorite = channel.favorite;
                has_program = channel.now_title[0] != '\0';
                badge_id = channel.id;
            }

            draw->AddRectFilled(ImVec2(list_x + 8.0f, y),
                                ImVec2(list_x + list_w - 8.0f, y + row_h - 6.0f),
                                item_index == selected_index ? selected : row,
                                14.0f);
            if (item_index == selected_index)
                draw->AddRectFilled(ImVec2(list_x + 8.0f, y + 9.0f), ImVec2(list_x + 13.0f, y + row_h - 15.0f), cyan, 3.0f);

            snprintf(index_text, sizeof(index_text), "%02d", (item_index + 1) % 100);
            draw->AddCircleFilled(ImVec2(list_x + 40.0f, y + 28.0f), 20.0f, channel_badge_color(badge_id));
            draw_sized_centered_text(draw, index_text, ImVec2(list_x + 40.0f, y + 27.0f), 14.0f, IM_COL32(255, 255, 255, 255));
            if (favorite)
                draw_home_text(draw, list_x + 482.0f, y + 7.0f, 19.0f, cyan, "*");
            draw_home_text(draw, list_x + 72.0f, y + 5.0f, 20.0f, black, name_text);
            if (home.iptv_sources_open)
            {
                draw_home_text(draw, list_x + 72.0f, y + 34.0f, 14.0f, muted, secondary_text);
                const char *guide_label = source_has_guide ? "Guide linked" : (source_local ? "M3U header" : "No guide");
                const float guide_w = source_has_guide ? 102.0f : 88.0f;
                draw->AddRectFilled(ImVec2(list_x + list_w - guide_w - 24.0f, y + 32.0f),
                                    ImVec2(list_x + list_w - 24.0f, y + 54.0f),
                                    source_has_guide ? IM_COL32(43, 137, 109, 238) : IM_COL32(218, 142, 42, 225),
                                    11.0f);
                draw_sized_centered_text(draw,
                                         guide_label,
                                         ImVec2(list_x + list_w - guide_w * 0.5f - 24.0f, y + 42.0f),
                                         11.0f,
                                         IM_COL32(255, 255, 255, 255));
            }
            else
            {
                const float badge_w = has_program ? 42.0f : 72.0f;
                draw->AddRectFilled(ImVec2(list_x + 72.0f, y + 34.0f),
                                    ImVec2(list_x + 72.0f + badge_w, y + 54.0f),
                                    has_program ? IM_COL32(236, 82, 73, 255) : IM_COL32(116, 115, 111, 210),
                                    10.0f);
                draw_sized_centered_text(draw,
                                         has_program ? "LIVE" : "NO EPG",
                                         ImVec2(list_x + 72.0f + badge_w * 0.5f, y + 43.0f),
                                         11.0f,
                                         IM_COL32(255, 255, 255, 255));
                draw_home_text(draw,
                               list_x + 72.0f + badge_w + 10.0f,
                               y + 34.0f,
                               14.0f,
                               has_program ? black : muted,
                               secondary_text);
                draw_home_text(draw, list_x + 526.0f, y + 8.0f, 13.0f, muted, group_text);
            }
        }
    }

    draw->AddRectFilled(ImVec2(details_x, rows_y - 12.0f),
                        ImVec2(details_x + details_w, rows_y + row_h * visible_rows + 12.0f),
                        row,
                        kPlayerControlRadius);
    draw_home_text(draw, details_x + 26.0f, rows_y + 12.0f, 17.0f, cyan, home.iptv_sources_open ? "Source details" : "Channel details");
    if (have_selected_channel)
    {
        char selected_name[96];
        char selected_source[96];
        char selected_url[160];
        char metadata[128];
        char now_window[40];
        char next_window[40];
        float programme_progress = 0.0f;
        const time_t now = time(nullptr);

        snprintf(selected_name, sizeof(selected_name), "%.26s", selected_channel.name);
        snprintf(selected_source,
                 sizeof(selected_source),
                 "%.22s  /  %.24s",
                 selected_channel.group[0] ? selected_channel.group : "Ungrouped",
                 selected_channel.source);
        snprintf(selected_url, sizeof(selected_url), "%.44s", selected_channel.url);
        snprintf(metadata,
                 sizeof(metadata),
                 "%s%.72s%s",
                 selected_channel.tvg_id[0] ? "tvg-id  " : "",
                 selected_channel.tvg_id,
                 selected_channel.logo_cached ? "  /  Logo cached" : (selected_channel.logo_url[0] ? "  /  Logo queued" : ""));
        format_program_window(selected_channel.now_start, selected_channel.now_stop, now_window, sizeof(now_window));
        format_program_window(selected_channel.next_start, selected_channel.next_stop, next_window, sizeof(next_window));
        if (selected_channel.now_start > 0 && selected_channel.now_stop > selected_channel.now_start)
        {
            const double elapsed = difftime(now, selected_channel.now_start);
            const double duration = difftime(selected_channel.now_stop, selected_channel.now_start);
            programme_progress = (float)std::max(0.0, std::min(1.0, elapsed / duration));
        }
        draw_home_text(draw, details_x + 26.0f, rows_y + 52.0f, 26.0f, black, selected_name);
        draw_home_text(draw, details_x + 26.0f, rows_y + 92.0f, 16.0f, muted, selected_source);
        draw_home_text(draw, details_x + 26.0f, rows_y + 122.0f, 14.0f, muted, selected_url);
        draw_home_text(draw, details_x + 26.0f, rows_y + 148.0f, 13.0f, muted, metadata);
        draw_home_text(draw, details_x + 26.0f, rows_y + 184.0f, 15.0f, cyan, "Now playing");
        if (now_window[0])
            draw_home_text(draw, details_x + 244.0f, rows_y + 184.0f, 14.0f, muted, now_window);
        draw_home_text(draw,
                       details_x + 26.0f,
                       rows_y + 210.0f,
                       18.0f,
                       black,
                       selected_channel.now_title[0] ? selected_channel.now_title : "No current EPG data");
        draw->AddRectFilled(ImVec2(details_x + 26.0f, rows_y + 246.0f),
                            ImVec2(details_x + details_w - 26.0f, rows_y + 252.0f),
                            IM_COL32(255, 255, 255, 30),
                            3.0f);
        if (programme_progress > 0.0f)
            draw->AddRectFilled(ImVec2(details_x + 26.0f, rows_y + 246.0f),
                                ImVec2(details_x + 26.0f + (details_w - 52.0f) * programme_progress, rows_y + 252.0f),
                                red,
                                3.0f);
        draw_home_text(draw, details_x + 26.0f, rows_y + 280.0f, 15.0f, cyan, "Up next");
        if (next_window[0])
            draw_home_text(draw, details_x + 244.0f, rows_y + 280.0f, 14.0f, muted, next_window);
        draw_home_text(draw,
                       details_x + 26.0f,
                       rows_y + 306.0f,
                       17.0f,
                       muted,
                       selected_channel.next_title[0] ? selected_channel.next_title : "No next programme data");
    }
    else if (have_selected_source)
    {
        char source_name[96];
        char source_url[160];
        char source_state[128];
        char guide_url[160];
        const bool guide_connected = selected_source.epg_url[0] != '\0';
        const ImU32 guide_bg = guide_connected ? IM_COL32(16, 53, 48, 235) : IM_COL32(62, 45, 20, 235);
        const ImU32 guide_accent = guide_connected ? IM_COL32(74, 190, 151, 255) : IM_COL32(235, 160, 66, 255);
        const char *guide_action;

        snprintf(source_name, sizeof(source_name), "%.28s", selected_source.name);
        snprintf(source_url, sizeof(source_url), "%.46s", selected_source.url);
        snprintf(source_state,
                 sizeof(source_state),
                 "%s  /  %d channel%s%s",
                 selected_source.local ? "Local file" : (selected_source.cache_ready ? "Cache ready" : "Not cached"),
                 selected_source.channel_count,
                 selected_source.channel_count == 1 ? "" : "s",
                 selected_source.refreshing ? "  /  Refreshing" : "");
        snprintf(guide_url,
                 sizeof(guide_url),
                 "%.46s",
                 guide_connected ? selected_source.epg_url : "No guide.xml address connected");
        if (selected_source.local)
            guide_action = guide_connected ? "Auto-detected from M3U" : "Add url-tvg to the M3U header";
        else
            guide_action = guide_connected ? "ZR  Change guide URL" : "ZR  Connect guide.xml";

        draw_home_text(draw, details_x + 26.0f, rows_y + 52.0f, 26.0f, black, source_name);
        draw_home_text(draw, details_x + 26.0f, rows_y + 92.0f, 15.0f, cyan, source_state);
        draw_home_text(draw, details_x + 26.0f, rows_y + 126.0f, 13.0f, muted, "Playlist address");
        draw_home_text(draw, details_x + 26.0f, rows_y + 148.0f, 14.0f, black, source_url);

        draw->AddRectFilled(ImVec2(details_x + 20.0f, rows_y + 180.0f),
                            ImVec2(details_x + details_w - 20.0f, rows_y + 320.0f),
                            guide_bg,
                            18.0f);
        draw->AddRectFilled(ImVec2(details_x + 20.0f, rows_y + 180.0f),
                            ImVec2(details_x + 26.0f, rows_y + 320.0f),
                            guide_accent,
                            3.0f);
        draw_home_text(draw, details_x + 40.0f, rows_y + 194.0f, 13.0f, muted, "Programme guide (EPG)");
        draw_home_text(draw,
                       details_x + 40.0f,
                       rows_y + 218.0f,
                       20.0f,
                       guide_accent,
                       guide_connected ? "Guide connected" : "Guide not connected");
        draw_home_text(draw,
                       details_x + 40.0f,
                       rows_y + 250.0f,
                       13.0f,
                       muted,
                       selected_source.local
                           ? (guide_connected ? "The guide address was read from the M3U header." : "Add url-tvg=\"guide.xml URL\" to the first M3U line.")
                           : (guide_connected ? "Now and next programmes are matched by tvg-id." : "Connect the guide.xml supplied with this playlist."));
        draw_home_text(draw, details_x + 40.0f, rows_y + 273.0f, 12.0f, muted, guide_url);
        draw->AddRectFilled(ImVec2(details_x + 40.0f, rows_y + 292.0f),
                            ImVec2(details_x + details_w - 40.0f, rows_y + 316.0f),
                            kPlayerSurface,
                            kPlayerControlRadius);
        draw_sized_centered_text(draw,
                                 guide_action,
                                 ImVec2(details_x + details_w * 0.5f, rows_y + 303.0f),
                                 12.0f,
                                 kPlayerText);

        draw_home_text(draw, details_x + 26.0f, rows_y + 342.0f, 13.0f, muted, "Source status");
        draw_home_text(draw, details_x + 26.0f, rows_y + 366.0f, 14.0f, black, selected_source.status);
    }
    else
    {
        draw_home_text(draw, details_x + 26.0f, rows_y + 54.0f, 21.0f, muted, "Nothing selected.");
    }

    if (have_selected_channel || have_selected_source)
    {
        draw->AddRectFilled(ImVec2((float)PLAYER_IPTV_ACTION_LEFT, (float)PLAYER_IPTV_ACTION_TOP),
                            ImVec2((float)PLAYER_IPTV_ACTION_RIGHT, (float)PLAYER_IPTV_ACTION_BOTTOM),
                            cyan,
                            kPlayerControlRadius);
        draw_sized_centered_text(draw,
                                 have_selected_source ? "Refresh source" : "Play channel",
                                 ImVec2((PLAYER_IPTV_ACTION_LEFT + PLAYER_IPTV_ACTION_RIGHT) * 0.5f,
                                        (PLAYER_IPTV_ACTION_TOP + PLAYER_IPTV_ACTION_BOTTOM) * 0.5f),
                                 15.0f,
                                 IM_COL32(255, 255, 255, 255));
    }

    draw->AddRectFilled(ImVec2(72.0f, height - 88.0f),
                        ImVec2(width - 72.0f, height - 44.0f),
                        kPlayerSurface,
                        kPlayerControlRadius);
    {
        char status[72];
        snprintf(status, sizeof(status), "%.48s", home.iptv_status);
        draw_home_text(draw, 92.0f, height - 76.0f, 14.0f, home.iptv_ready ? muted : red, status);
    }
    if (home.iptv_sources_open)
    {
        const char *guide_label = have_selected_source && selected_source.local ? "Guide Help" : "Set Guide";
        const SwitchActionHint hints[] = {
            {"A", "Refresh"}, {"Y", "Add M3U"}, {"ZR", guide_label}, {"-", "Remove"}, {"X", "Channels"}, {"B", "Close"},
        };
        draw_switch_action_hints(draw,
                                 width - 92.0f,
                                 height - 66.0f,
                                 hints,
                                 (int)(sizeof(hints) / sizeof(hints[0])),
                                 true);
    }
    else
    {
        const SwitchActionHint hints[] = {
            {"A", "Play"}, {"Y", "Favorite"}, {"ZL/ZR", "Filter"}, {"L/R", "Page"}, {"X", "Sources"}, {"B", "Close"},
        };
        draw_switch_action_hints(draw,
                                 width - 92.0f,
                                 height - 66.0f,
                                 hints,
                                 (int)(sizeof(hints) / sizeof(hints[0])),
                                 true);
    }
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
    show_iptv_panel = ctx->home_state_valid && ctx->home_state.iptv_panel_open;
    if (!has_player_overlay && !show_iptv_panel)
        return false;

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
            draw_progress_bar(draw, overlay.bar, ctx->status.player_state, io->DisplaySize.x, io->DisplaySize.y);
        draw_video_action_hints(draw,
                                io->DisplaySize.x,
                                io->DisplaySize.y,
                                ctx->home_state_valid && ctx->home_state.iptv_channel_count > 1);
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
    if (ctx->home_state_valid && ctx->home_state.iptv_panel_open)
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
    if (!ctx->home_state_valid || !ctx->home_state.iptv_panel_open)
    {
        draw_video_action_hints(draw,
                                io->DisplaySize.x,
                                io->DisplaySize.y,
                                ctx->home_state_valid && ctx->home_state.iptv_channel_count > 1);
    }
    ImGui::Render();

    if (!render_draw_data(ctx, slot))
        return false;
    ctx->dk3d_overlay_dirty = true;
    return true;
}
