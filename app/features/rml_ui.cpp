// RmlUi drawn by RT64 over the game picture (see rml_ui.hpp and
// docs/anotherworld/design-organize-screen.md, section 4).
//
// RT64 calls three render hooks (rhi/rt64_render_hooks.h): init with the RHI
// and device when the renderer starts, draw on every present with the command
// list and the swap-chain framebuffer (already bound, the game picture already
// drawn), and deinit. RmlUi lives entirely on that render thread: it is
// initialised in the init hook and updated and rendered in the draw hook.
// Input arrives on the main thread (SDL), so it is queued and replayed in the
// draw hook.
//
// The render interface is RmlUi 6's minimum set: compiled geometry in upload-
// heap vertex/index buffers, RGBA8 textures uploaded through an upload buffer
// on the present command list, and scissoring. Untextured geometry binds a 1x1
// white texture. Resources RmlUi releases are kept for a few presents before
// they are destroyed, so the GPU is never using a freed buffer.

#include "rml_ui.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <RmlUi/Core.h>

// A single-header PNG decoder, static to this file (class_art.cpp,
// hd_backgrounds.cpp and RT64 compile their own copies).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#include "stb/stb_image.h"

#include "common/rt64_plume.h"
#include "rhi/rt64_render_hooks.h"

#include "organize_view.hpp"
#include "options_view.hpp"
#include "alerts_view.hpp"
#include "battle_labels.hpp"
#include "field_view.hpp"
#include "portraits.hpp"

#include "shaders/RmlUiPS.hlsl.spirv.h"
#include "shaders/RmlUiVS.hlsl.spirv.h"
#ifdef _WIN32
#   include "shaders/RmlUiPS.hlsl.dxil.h"
#   include "shaders/RmlUiVS.hlsl.dxil.h"
#endif
#ifdef __APPLE__
#   include "shaders/RmlUiPS.hlsl.metal.h"
#   include "shaders/RmlUiVS.hlsl.metal.h"
#endif

namespace ogre::rml_ui {
namespace {

constexpr uint64_t kReleaseDelay = 4;  // presents before a released resource is destroyed

struct PushConstants {
    float translation[2];
    float inverse_half_size[2];
};

struct TextureSet : RenderDescriptorSetBase {
    uint32_t gTexture = 0;

    TextureSet(const RenderSampler* sampler, RenderDevice* device) {
        builder.begin();
        gTexture = builder.addTexture(1);
        builder.addImmutableSampler(2, sampler);
        builder.end();
        if (device != nullptr) {
            create(device);
        }
    }
};

struct Geometry {
    std::unique_ptr<RenderBuffer> vertices;
    std::unique_ptr<RenderBuffer> indices;
    uint32_t vertex_bytes = 0;
    uint32_t index_bytes = 0;
    uint32_t index_count = 0;
};

struct Texture {
    std::unique_ptr<RenderTexture> texture;
    std::unique_ptr<RenderBuffer> upload;  // kept until the copy has run
    std::unique_ptr<TextureSet> set;
    bool uploaded = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t row_pixels = 0;
};

// Shader blobs for the backend RT64 chose.
struct Blob {
    const void* data = nullptr;
    uint64_t size = 0;
};

Blob vertex_blob(RenderShaderFormat format) {
#ifdef _WIN32
    if (format == RenderShaderFormat::DXIL) return {RmlUiVSBlobDXIL, sizeof(RmlUiVSBlobDXIL)};
#endif
#ifdef __APPLE__
    if (format == RenderShaderFormat::METAL) return {RmlUiVSBlobMSL, sizeof(RmlUiVSBlobMSL)};
#endif
    return {RmlUiVSBlobSPIRV, sizeof(RmlUiVSBlobSPIRV)};
}

Blob pixel_blob(RenderShaderFormat format) {
#ifdef _WIN32
    if (format == RenderShaderFormat::DXIL) return {RmlUiPSBlobDXIL, sizeof(RmlUiPSBlobDXIL)};
#endif
#ifdef __APPLE__
    if (format == RenderShaderFormat::METAL) return {RmlUiPSBlobMSL, sizeof(RmlUiPSBlobMSL)};
#endif
    return {RmlUiPSBlobSPIRV, sizeof(RmlUiPSBlobSPIRV)};
}

class RenderInterfaceRT64 final : public Rml::RenderInterface {
public:
    bool init(plume::RenderInterface* rhi, RenderDevice* device);

    void begin_frame(RenderCommandList* list, RenderFramebuffer* framebuffer);
    void end_frame();
    void destroy();

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle handle) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

private:
    Texture* make_texture(const uint8_t* rgba, uint32_t width, uint32_t height);
    void upload(Texture& texture);
    void apply_scissor();

    RenderDevice* device_ = nullptr;
    std::unique_ptr<RenderPipelineLayout> layout_;
    std::unique_ptr<RenderPipeline> pipeline_;
    std::unique_ptr<RenderSampler> sampler_;
    Texture* white_ = nullptr;

    RenderCommandList* list_ = nullptr;
    RenderFramebuffer* framebuffer_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool scissor_enabled_ = false;
    Rml::Rectanglei scissor_ = Rml::Rectanglei::FromSize({0, 0});
    bool framebuffer_bound_ = false;

    uint64_t frame_ = 0;
    std::deque<std::pair<uint64_t, std::unique_ptr<Geometry>>> dead_geometry_;
    std::deque<std::pair<uint64_t, std::unique_ptr<Texture>>> dead_textures_;
    std::unordered_map<Geometry*, std::unique_ptr<Geometry>> geometry_;
    std::unordered_map<Texture*, std::unique_ptr<Texture>> textures_;
};

bool RenderInterfaceRT64::init(plume::RenderInterface* rhi, RenderDevice* device) {
    device_ = device;
    const RenderShaderFormat format = rhi->getCapabilities().shaderFormat;

    RenderSamplerDesc sampler_desc;
    sampler_desc.minFilter = RenderFilter::LINEAR;
    sampler_desc.magFilter = RenderFilter::LINEAR;
    sampler_desc.addressU = RenderTextureAddressMode::CLAMP;
    sampler_desc.addressV = RenderTextureAddressMode::CLAMP;
    sampler_desc.addressW = RenderTextureAddressMode::CLAMP;
    sampler_ = device->createSampler(sampler_desc);

    TextureSet set_layout(sampler_.get(), nullptr);
    RenderPipelineLayoutBuilder layout_builder;
    layout_builder.begin(false, true);
    layout_builder.addPushConstant(0, 0, sizeof(PushConstants), RenderShaderStageFlag::VERTEX);
    layout_builder.addDescriptorSet(set_layout);
    layout_builder.end();
    layout_ = layout_builder.create(device);

    const Blob vs = vertex_blob(format);
    const Blob ps = pixel_blob(format);
    std::unique_ptr<plume::RenderShader> vertex_shader = device->createShader(vs.data, vs.size, "VSMain", format);
    std::unique_ptr<plume::RenderShader> pixel_shader = device->createShader(ps.data, ps.size, "PSMain", format);

    const RenderInputSlot slot(0, sizeof(Rml::Vertex));
    const RenderInputElement elements[] = {
        RenderInputElement("POSITION", 0, 0, RenderFormat::R32G32_FLOAT, 0, offsetof(Rml::Vertex, position)),
        RenderInputElement("COLOR", 0, 1, RenderFormat::R8G8B8A8_UNORM, 0, offsetof(Rml::Vertex, colour)),
        RenderInputElement("TEXCOORD", 0, 2, RenderFormat::R32G32_FLOAT, 0, offsetof(Rml::Vertex, tex_coord)),
    };

    // Premultiplied alpha, as RmlUi produces it.
    RenderBlendDesc blend;
    blend.blendEnabled = true;
    blend.srcBlend = RenderBlend::ONE;
    blend.dstBlend = RenderBlend::INV_SRC_ALPHA;
    blend.blendOp = RenderBlendOperation::ADD;
    blend.srcBlendAlpha = RenderBlend::ONE;
    blend.dstBlendAlpha = RenderBlend::INV_SRC_ALPHA;
    blend.blendOpAlpha = RenderBlendOperation::ADD;

    RenderGraphicsPipelineDesc desc;
    desc.pipelineLayout = layout_.get();
    desc.vertexShader = vertex_shader.get();
    desc.pixelShader = pixel_shader.get();
    desc.renderTargetFormat[0] = RenderFormat::B8G8R8A8_UNORM;  // RT64's swap chain, as its VI pipeline
    desc.renderTargetBlend[0] = blend;
    desc.renderTargetCount = 1;
    desc.inputSlots = &slot;
    desc.inputSlotsCount = 1;
    desc.inputElements = elements;
    desc.inputElementsCount = static_cast<uint32_t>(std::size(elements));
    pipeline_ = device->createGraphicsPipeline(desc);

    const uint8_t white[4] = {255, 255, 255, 255};
    white_ = make_texture(white, 1, 1);
    return pipeline_ != nullptr;
}

Texture* RenderInterfaceRT64::make_texture(const uint8_t* rgba, uint32_t width, uint32_t height) {
    auto texture = std::make_unique<Texture>();
    texture->width = width;
    texture->height = height;
    texture->texture = device_->createTexture(RenderTextureDesc::Texture2D(width, height, 1, RenderFormat::R8G8B8A8_UNORM));
    // D3D12 needs 256-byte rows in a placed footprint.
    texture->row_pixels = ((width * 4 + 255) / 256 * 256) / 4;
    const uint64_t bytes = uint64_t(texture->row_pixels) * 4 * height;
    texture->upload = device_->createBuffer(RenderBufferDesc::UploadBuffer(bytes));
    auto* dst = static_cast<uint8_t*>(texture->upload->map());
    for (uint32_t y = 0; y < height; y++) {
        std::memcpy(dst + uint64_t(y) * texture->row_pixels * 4, rgba + uint64_t(y) * width * 4, width * 4);
    }
    texture->upload->unmap();
    texture->set = std::make_unique<TextureSet>(sampler_.get(), device_);
    texture->set->setTexture(texture->set->gTexture, texture->texture.get(), RenderTextureLayout::SHADER_READ);
    Texture* raw = texture.get();
    textures_[raw] = std::move(texture);
    return raw;
}

void RenderInterfaceRT64::upload(Texture& texture) {
    if (texture.uploaded || list_ == nullptr) {
        return;
    }
    list_->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture.texture.get(), RenderTextureLayout::COPY_DEST));
    list_->copyTextureRegion(
        RenderTextureCopyLocation::Subresource(texture.texture.get()),
        RenderTextureCopyLocation::PlacedFootprint(texture.upload.get(), RenderFormat::R8G8B8A8_UNORM, texture.width,
                                                   texture.height, 1, texture.row_pixels));
    list_->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(texture.texture.get(), RenderTextureLayout::SHADER_READ));
    texture.uploaded = true;
    framebuffer_bound_ = false;  // a copy ends the render pass on some backends
    // The upload buffer is released with the texture; it is small.
}

void RenderInterfaceRT64::begin_frame(RenderCommandList* list, RenderFramebuffer* framebuffer) {
    list_ = list;
    framebuffer_ = framebuffer;
    width_ = framebuffer->getWidth();
    height_ = framebuffer->getHeight();
    framebuffer_bound_ = false;
    scissor_enabled_ = false;
    frame_++;
    while (!dead_geometry_.empty() && dead_geometry_.front().first + kReleaseDelay <= frame_) {
        dead_geometry_.pop_front();
    }
    while (!dead_textures_.empty() && dead_textures_.front().first + kReleaseDelay <= frame_) {
        dead_textures_.pop_front();
    }
}

void RenderInterfaceRT64::end_frame() {
    list_ = nullptr;
    framebuffer_ = nullptr;
}

void RenderInterfaceRT64::destroy() {
    dead_geometry_.clear();
    dead_textures_.clear();
    geometry_.clear();
    textures_.clear();
    pipeline_.reset();
    layout_.reset();
    sampler_.reset();
    white_ = nullptr;
}

Rml::CompiledGeometryHandle RenderInterfaceRT64::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                                 Rml::Span<const int> indices) {
    if (vertices.empty() || indices.empty()) {
        return 0;
    }
    auto geometry = std::make_unique<Geometry>();
    geometry->vertex_bytes = static_cast<uint32_t>(vertices.size() * sizeof(Rml::Vertex));
    geometry->index_bytes = static_cast<uint32_t>(indices.size() * sizeof(int));
    geometry->index_count = static_cast<uint32_t>(indices.size());
    geometry->vertices = device_->createBuffer(RenderBufferDesc::VertexBuffer(geometry->vertex_bytes, RenderHeapType::UPLOAD));
    geometry->indices = device_->createBuffer(RenderBufferDesc::IndexBuffer(geometry->index_bytes, RenderHeapType::UPLOAD));
    std::memcpy(geometry->vertices->map(), vertices.data(), geometry->vertex_bytes);
    geometry->vertices->unmap();
    std::memcpy(geometry->indices->map(), indices.data(), geometry->index_bytes);
    geometry->indices->unmap();
    Geometry* raw = geometry.get();
    geometry_[raw] = std::move(geometry);
    return reinterpret_cast<Rml::CompiledGeometryHandle>(raw);
}

void RenderInterfaceRT64::apply_scissor() {
    RenderRect rect(0, 0, static_cast<int32_t>(width_), static_cast<int32_t>(height_));
    if (scissor_enabled_) {
        rect = RenderRect(std::max(0, scissor_.Left()), std::max(0, scissor_.Top()),
                          std::min(static_cast<int32_t>(width_), scissor_.Right()),
                          std::min(static_cast<int32_t>(height_), scissor_.Bottom()));
    }
    list_->setScissors(&rect, 1);
}

void RenderInterfaceRT64::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                                         Rml::TextureHandle texture_handle) {
    auto* geometry = reinterpret_cast<Geometry*>(handle);
    if (geometry == nullptr || list_ == nullptr) {
        return;
    }
    Texture* texture = texture_handle != 0 ? reinterpret_cast<Texture*>(texture_handle) : white_;
    upload(*texture);
    if (!framebuffer_bound_) {
        list_->setFramebuffer(framebuffer_);
        framebuffer_bound_ = true;
    }
    const RenderViewport viewport(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    list_->setViewports(&viewport, 1);
    apply_scissor();
    list_->setGraphicsPipelineLayout(layout_.get());
    list_->setPipeline(pipeline_.get());
    list_->setGraphicsDescriptorSet(texture->set->get(), 0);
    const PushConstants constants = {{translation.x, translation.y},
                                     {2.0f / static_cast<float>(width_), 2.0f / static_cast<float>(height_)}};
    list_->setGraphicsPushConstants(0, &constants);
    const RenderInputSlot slot(0, sizeof(Rml::Vertex));
    const RenderVertexBufferView vertex_view(RenderBufferReference(geometry->vertices.get()), geometry->vertex_bytes);
    list_->setVertexBuffers(0, &vertex_view, 1, &slot);
    const RenderIndexBufferView index_view(RenderBufferReference(geometry->indices.get()), geometry->index_bytes,
                                           RenderFormat::R32_UINT);
    list_->setIndexBuffer(&index_view);
    list_->drawIndexedInstanced(geometry->index_count, 1, 0, 0, 0);
}

void RenderInterfaceRT64::ReleaseGeometry(Rml::CompiledGeometryHandle handle) {
    auto it = geometry_.find(reinterpret_cast<Geometry*>(handle));
    if (it != geometry_.end()) {
        dead_geometry_.emplace_back(frame_, std::move(it->second));
        geometry_.erase(it);
    }
}

Rml::TextureHandle RenderInterfaceRT64::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
    // `portrait:<class>` is the game's speaker portrait for that class, decoded
    // from the ROM (portraits.cpp). RmlUi joins an <img> src onto the
    // document's folder, so the scheme is found anywhere in the string.
    // `marker:leader`: the gold arrow over a unit's leader, after the one the
    // game's formation screen draws. A 12x10 pixel-art arrow pointing down,
    // gold with a dark outline, upscaled 4x with nearest sampling.
    if (source.find("marker:leader") != Rml::String::npos) {
        static const char* const kArrow[] = {
            "############",
            "#gggggggggg#",
            "#gGGGGGGGGg#",
            ".#gGGGGGGg#.",
            "..#gGGGGg#..",
            "...#gGGg#...",
            "....#gg#....",
            ".....##.....",
        };
        constexpr int kW = 12;
        constexpr int kH = 8;
        constexpr int kScale = 4;
        std::vector<uint8_t> rgba(static_cast<size_t>(kW * kScale) * kH * kScale * 4, 0);
        for (int y = 0; y < kH; y++) {
            for (int x = 0; x < kW; x++) {
                const char c = kArrow[y][x];
                uint8_t px[4] = {0, 0, 0, 0};
                if (c == '#') { px[0] = 0x3A; px[1] = 0x2A; px[2] = 0x14; px[3] = 0xFF; }
                else if (c == 'g') { px[0] = 0xF8; px[1] = 0xE6; px[2] = 0x8A; px[3] = 0xFF; }
                else if (c == 'G') { px[0] = 0xE0; px[1] = 0xB0; px[2] = 0x2C; px[3] = 0xFF; }
                else { continue; }
                for (int dy = 0; dy < kScale; dy++) {
                    for (int dx = 0; dx < kScale; dx++) {
                        const size_t o = (static_cast<size_t>(y * kScale + dy) * kW * kScale + x * kScale + dx) * 4;
                        std::memcpy(&rgba[o], px, 4);
                    }
                }
            }
        }
        dimensions = Rml::Vector2i(kW * kScale, kH * kScale);
        return reinterpret_cast<Rml::TextureHandle>(make_texture(rgba.data(), kW * kScale, kH * kScale));
    }
    // `icon:<item>` (consumable) and `equip:<item>`: menu icons (portraits.cpp).
    for (const bool equipment : {false, true}) {
        const char* scheme = equipment ? "equip:" : "icon:";
        if (const size_t icon_at = source.find(scheme); icon_at != Rml::String::npos) {
            const int item = std::atoi(source.c_str() + icon_at + std::strlen(scheme));
            std::vector<uint8_t> rgba;
            if (!portraits::decode_icon(equipment, item, rgba)) {
                return 0;
            }
            const int size = portraits::ICON_SIZE * portraits::ICON_SCALE;
            dimensions = Rml::Vector2i(size, size);
            return reinterpret_cast<Rml::TextureHandle>(make_texture(rgba.data(), size, size));
        }
    }
    // `sprite:<class>`: the class's formation sprite (portraits.cpp).
    if (const size_t sprite_at = source.find("sprite:"); sprite_at != Rml::String::npos) {
        // `sprite:<class>:<model offset>`; the offset is optional.
        char* end = nullptr;
        const int cls = static_cast<int>(std::strtol(source.c_str() + sprite_at + 7, &end, 10));
        const int offset = (end != nullptr && *end == ':') ? std::atoi(end + 1) : 0;
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        if (!portraits::decode_sprite(cls, rgba, width, height, offset)) {
            return 0;
        }
        dimensions = Rml::Vector2i(width, height);
        return reinterpret_cast<Rml::TextureHandle>(
            make_texture(rgba.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height)));
    }
    const size_t at = source.find("portrait:");
    if (at != Rml::String::npos) {
        const int cls = std::atoi(source.c_str() + at + 9);
        std::vector<uint8_t> rgba;
        if (!portraits::decode(cls, 0, rgba)) {
            return 0;
        }
        dimensions = Rml::Vector2i(portraits::WIDTH, portraits::HEIGHT);
        return reinterpret_cast<Rml::TextureHandle>(make_texture(rgba.data(), portraits::WIDTH, portraits::HEIGHT));
    }
    // `pixel:<path>`: pixel art (a mod's battle label, battle_labels.cpp),
    // enlarged 8x with nearest sampling so the element's scaling stays crisp;
    // the file is read as below.
    int enlarge = 1;
    Rml::String file = source;
    if (const size_t pixel_at = source.find("pixel:"); pixel_at != Rml::String::npos) {
        enlarge = 8;
        file = source.substr(pixel_at + 6);
    }
    // A PNG file named by the port's own documents in ui/ (the MODS tab's
    // icons) or a mod's label image (a path inside its pack's folder,
    // content_db.hpp Dot::label_image). Other formats and files over 4 MiB
    // are refused.
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::u8path(file);
    if (path.extension() != ".png" || !std::filesystem::is_regular_file(path, ec) ||
        std::filesystem::file_size(path, ec) > 4u * 1024u * 1024u) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "rml_ui: only PNG files up to 4 MiB load (%s)", source.c_str());
        return 0;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    uint8_t* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "rml_ui: cannot read %s: %s", source.c_str(), stbi_failure_reason());
        stbi_image_free(pixels);
        return 0;
    }
    // RmlUi blends premultiplied alpha.
    std::vector<uint8_t> rgba(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        for (int c = 0; c < 3; c++) {
            rgba[i + c] = static_cast<uint8_t>((rgba[i + c] * rgba[i + 3] + 127) / 255);
        }
    }
    if (enlarge > 1 && width <= 512 && height <= 512) {
        std::vector<uint8_t> big(static_cast<size_t>(width) * enlarge * height * enlarge * 4);
        for (int y = 0; y < height * enlarge; y++) {
            for (int x = 0; x < width * enlarge; x++) {
                std::memcpy(&big[(static_cast<size_t>(y) * width * enlarge + x) * 4],
                            &rgba[(static_cast<size_t>(y / enlarge) * width + x / enlarge) * 4], 4);
            }
        }
        rgba.swap(big);
        width *= enlarge;
        height *= enlarge;
    }
    dimensions = Rml::Vector2i(width, height);
    return reinterpret_cast<Rml::TextureHandle>(
        make_texture(rgba.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height)));
}

Rml::TextureHandle RenderInterfaceRT64::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) {
    if (dimensions.x <= 0 || dimensions.y <= 0) {
        return 0;
    }
    return reinterpret_cast<Rml::TextureHandle>(
        make_texture(source.data(), static_cast<uint32_t>(dimensions.x), static_cast<uint32_t>(dimensions.y)));
}

void RenderInterfaceRT64::ReleaseTexture(Rml::TextureHandle handle) {
    auto it = textures_.find(reinterpret_cast<Texture*>(handle));
    if (it != textures_.end()) {
        dead_textures_.emplace_back(frame_, std::move(it->second));
        textures_.erase(it);
    }
}

void RenderInterfaceRT64::EnableScissorRegion(bool enable) {
    scissor_enabled_ = enable;
}

void RenderInterfaceRT64::SetScissorRegion(Rml::Rectanglei region) {
    scissor_ = region;
}

class SystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        static const char* const kTypes[] = {"always", "error", "assert", "warning", "info", "debug"};
        const int index = static_cast<int>(type);
        std::fprintf(stderr, "[rmlui] %s: %s\n", index >= 0 && index < 6 ? kTypes[index] : "?", message.c_str());
        std::fflush(stderr);
        return true;
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

// Input queued on the main thread and replayed on the render thread.
struct InputEvent {
    enum class Kind { Move, Down, Up, Wheel, Key } kind;
    int x = 0;
    int y = 0;
    int button = 0;
    float wheel = 0.0f;
    Rml::Input::KeyIdentifier key = Rml::Input::KI_UNKNOWN;
    bool pad = false;
};

struct State {
    std::filesystem::path asset_dir;
    RenderInterfaceRT64 render;
    SystemInterface system;
    Rml::Context* context = nullptr;
    bool ready = false;
    std::mutex input_mutex;
    std::vector<InputEvent> input;
};

State& state() {
    static State s;
    return s;
}

bool enabled() {
    static const bool value = [] {
        const char* env = std::getenv("OGRE_RMLUI");
        return env == nullptr || env[0] != '0';
    }();
    return value;
}


void init_hook(plume::RenderInterface* rhi, RenderDevice* device) {
    State& s = state();
    if (!enabled() || !s.render.init(rhi, device)) {
        std::fprintf(stderr, "[rmlui] disabled (OGRE_RMLUI=0 or the pipeline could not be created)\n");
        return;
    }
    Rml::SetRenderInterface(&s.render);
    Rml::SetSystemInterface(&s.system);
    if (!Rml::Initialise()) {
        std::fprintf(stderr, "[rmlui] Rml::Initialise failed\n");
        return;
    }
    const std::filesystem::path font = s.asset_dir / "fonts" / "LatoLatin-Regular.ttf";
    if (!Rml::LoadFontFace(font.string())) {
        std::fprintf(stderr, "[rmlui] could not load %s\n", font.string().c_str());
    }
    Rml::LoadFontFace((s.asset_dir / "fonts" / "LatoLatin-Bold.ttf").string());
    s.context = Rml::CreateContext("main", Rml::Vector2i(1280, 720));
    if (s.context != nullptr) {
        field_view::init(s.context, s.asset_dir);
        alerts_view::init(s.context, s.asset_dir);
        battle_labels::init(s.context, s.asset_dir);
        organize_view::init(s.context, s.asset_dir);
        // The Esc menu loads after the Organize view so it draws on top.
        options_view::init(s.context, s.asset_dir);
    }
    s.ready = s.context != nullptr;
    std::fprintf(stderr, "[rmlui] ready, assets %s\n", s.asset_dir.string().c_str());
    std::fflush(stderr);
}

bool is_text_field(const Rml::Element* e) {
    if (e == nullptr) {
        return false;
    }
    if (e->GetTagName() == "textarea") {
        return true;
    }
    if (e->GetTagName() != "input") {
        return false;
    }
    const Rml::String type = e->GetAttribute<Rml::String>("type", "text");
    return type == "text" || type == "password";
}

bool focusable(Rml::Element* e) {
    return e->GetComputedValues().tab_index() == Rml::Style::TabIndex::Auto && e->IsVisible(true) &&
           !e->IsPseudoClassSet("disabled") && e->GetBox().GetSize(Rml::BoxArea::Border).x > 0.0f;
}

void collect_focusable(Rml::Element* e, std::vector<Rml::Element*>& out) {
    const int count = e->GetNumChildren();
    for (int i = 0; i < count; i++) {
        Rml::Element* child = e->GetChild(i);
        if (child->GetComputedValues().display() == Rml::Style::Display::None) {
            continue;
        }
        if (focusable(child)) {
            out.push_back(child);
        }
        collect_focusable(child, out);
    }
}

bool is_ancestor(const Rml::Element* a, const Rml::Element* of) {
    for (const Rml::Element* p = of->GetParentNode(); p != nullptr; p = p->GetParentNode()) {
        if (p == a) {
            return true;
        }
    }
    return false;
}

// The nearest focusable element of the focused document in a direction. The
// score is RmlUi's (the gap along the direction, plus a heavy penalty for
// lying outside the source's projection), with the distance between centres
// across the direction as a tie-break so a column stays a column. A control
// inside the source (a row's button) or around it counts only left and right,
// so up and down walk the rows.
Rml::Element* navigate(Rml::Element* from, Rml::Input::KeyIdentifier key) {
    Rml::ElementDocument* document = from->GetOwnerDocument();
    if (document == nullptr) {
        return nullptr;
    }
    std::vector<Rml::Element*> all;
    collect_focusable(document, all);
    const bool horizontal = key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT;
    const float sign = (key == Rml::Input::KI_RIGHT || key == Rml::Input::KI_DOWN) ? 1.0f : -1.0f;
    const int axis = horizontal ? 0 : 1;
    const int cross = 1 - axis;
    const Rml::Vector2f a0 = from->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f a1 = a0 + from->GetBox().GetSize(Rml::BoxArea::Border);
    const float ac[2] = {(a0.x + a1.x) * 0.5f, (a0.y + a1.y) * 0.5f};
    const float amin[2] = {a0.x, a0.y}, amax[2] = {a1.x, a1.y};
    Rml::Element* best = nullptr;
    float best_score = 1e30f;
    for (Rml::Element* e : all) {
        if (e == from) {
            continue;
        }
        if (!horizontal && (is_ancestor(e, from) || is_ancestor(from, e))) {
            continue;
        }
        const Rml::Vector2f b0 = e->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f b1 = b0 + e->GetBox().GetSize(Rml::BoxArea::Border);
        const float bc[2] = {(b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f};
        const float bmin[2] = {b0.x, b0.y}, bmax[2] = {b1.x, b1.y};
        const float along = (bc[axis] - ac[axis]) * sign;
        if (along < 1.0f) {
            continue;
        }
        const float gap = std::max(0.0f, sign > 0 ? bmin[axis] - amax[axis] : amin[axis] - bmax[axis]);
        const float outside = std::max(0.0f, bmin[cross] - amax[cross]) + std::max(0.0f, amin[cross] - bmax[cross]);
        const float score = gap + outside * 1000.0f + std::abs(bc[cross] - ac[cross]) * 0.5f;
        if (score < best_score) {
            best_score = score;
            best = e;
        }
    }
    return best;
}

void replay_input(State& s) {
    std::vector<InputEvent> events;
    {
        std::lock_guard<std::mutex> lock(s.input_mutex);
        events.swap(s.input);
    }
    for (const InputEvent& e : events) {
        switch (e.kind) {
            case InputEvent::Kind::Move: s.context->ProcessMouseMove(e.x, e.y, 0); break;
            case InputEvent::Kind::Down: {
                s.context->ProcessMouseButtonDown(e.button, 0);
                // Which element a press lands on (the Esc menu's click check).
                const Rml::Element* hover = s.context->GetHoverElement();
                std::fprintf(stderr, "[rmlui] press on %s\n",
                             hover != nullptr ? hover->GetAddress().c_str() : "(nothing)");
                break;
            }
            case InputEvent::Kind::Up: s.context->ProcessMouseButtonUp(e.button, 0); break;
            case InputEvent::Kind::Wheel: s.context->ProcessMouseWheel(Rml::Vector2f(0.0f, e.wheel), 0); break;
            case InputEvent::Kind::Key:
                // Confirm clicks the focused element, so the pad's A and Enter act
                // like a mouse click on whatever navigation reached.
                if (e.key == Rml::Input::KI_RETURN) {
                    if (Rml::Element* focus = s.context->GetFocusElement()) {
                        focus->Click();
                    }
                    break;
                }
                if (e.key == Rml::Input::KI_UP || e.key == Rml::Input::KI_DOWN || e.key == Rml::Input::KI_LEFT ||
                    e.key == Rml::Input::KI_RIGHT) {
                    Rml::Element* focus = s.context->GetFocusElement();
                    const bool caret = is_text_field(focus) && !e.pad &&
                                       (e.key == Rml::Input::KI_LEFT || e.key == Rml::Input::KI_RIGHT);
                    if (caret || focus == nullptr || focus->GetOwnerDocument() == focus) {
                        s.context->ProcessKeyDown(e.key, 0);
                        s.context->ProcessKeyUp(e.key, 0);
                        break;
                    }
                    if (Rml::Element* next = navigate(focus, e.key)) {
                        next->Focus(true);
                        next->ScrollIntoView(Rml::ScrollAlignment::Nearest);
                    }
                    break;
                }
                s.context->ProcessKeyDown(e.key, 0);
                s.context->ProcessKeyUp(e.key, 0);
                break;
        }
    }
}

void draw_hook(RenderCommandList* list, RenderFramebuffer* framebuffer) {
    State& s = state();
    if (!s.ready) {
        return;
    }
    s.render.begin_frame(list, framebuffer);
    const int width = static_cast<int>(framebuffer->getWidth());
    const int height = static_cast<int>(framebuffer->getHeight());
    s.context->SetDimensions(Rml::Vector2i(width, height));
    // Documents are laid out on a 1280x720 dp canvas; one dp is scaled so the
    // canvas fits the window, so small windows do not squash the layout and
    // wide ones do not stretch it (the spare width is left to the backdrop).
    s.context->SetDensityIndependentPixelRatio(
        std::max(0.25f, std::min(static_cast<float>(width) / 1280.0f, static_cast<float>(height) / 720.0f)));
    field_view::update();
    alerts_view::update();
    battle_labels::update();
    organize_view::update();
    options_view::update();
    replay_input(s);
    s.context->Update();
    s.context->Render();
    s.render.end_frame();
}

void deinit_hook() {
    State& s = state();
    if (s.ready) {
        Rml::Shutdown();
        s.ready = false;
    }
    s.render.destroy();
}

void push(const InputEvent& e) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.input_mutex);
    if (s.input.size() < 256) {
        s.input.push_back(e);
    }
}

}  // namespace

void install(const std::filesystem::path& asset_dir) {
    state().asset_dir = asset_dir;
    RT64::SetRenderHooks(init_hook, draw_hook, deinit_hook);
}

void mouse_move(int x, int y) {
    push({InputEvent::Kind::Move, x, y});
}

void mouse_button(int button, bool down) {
    InputEvent e{down ? InputEvent::Kind::Down : InputEvent::Kind::Up};
    e.button = button;
    push(e);
}

void mouse_wheel(float delta) {
    InputEvent e{InputEvent::Kind::Wheel};
    e.wheel = delta;
    push(e);
}

void nav_key(NavKey key, bool pad) {
    InputEvent e{InputEvent::Kind::Key};
    e.pad = pad;
    switch (key) {
        case NavKey::Up: e.key = Rml::Input::KI_UP; break;
        case NavKey::Down: e.key = Rml::Input::KI_DOWN; break;
        case NavKey::Left: e.key = Rml::Input::KI_LEFT; break;
        case NavKey::Right: e.key = Rml::Input::KI_RIGHT; break;
        case NavKey::Confirm: e.key = Rml::Input::KI_RETURN; break;
        case NavKey::Back: e.key = Rml::Input::KI_ESCAPE; break;
        case NavKey::Next: e.key = Rml::Input::KI_TAB; break;
    }
    push(e);
}

}  // namespace ogre::rml_ui

