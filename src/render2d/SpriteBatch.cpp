#include "render2d/SpriteBatch.h"

#include "core/Paths.h"
#include "gfx/VulkanContext.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <cstddef>

namespace render2d {

namespace {

uint32_t packColor(const Color& c)
{
    const glm::vec4 v = glm::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f;
    return uint32_t(v.r) | (uint32_t(v.g) << 8) | (uint32_t(v.b) << 16) | (uint32_t(v.a) << 24);
}

} // namespace

SpriteBatch::SpriteBatch(gfx::VulkanContext& ctx, gfx::Renderer& renderer, uint32_t maxSprites)
    : maxSprites_(maxSprites)
{
    pipeline_ = gfx::PipelineBuilder()
                    .shaders(core::shaderPath("sprite.vert.spv"), core::shaderPath("sprite.frag.spv"))
                    .vertexBinding(0, sizeof(Vertex))
                    .vertexAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position))
                    .vertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv))
                    .vertexAttribute(2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, color))
                    .blend(gfx::BlendMode::Alpha)
                    .colorFormat(renderer.colorFormat())
                    .depthFormat(renderer.depthFormat())
                    .depthTest(false, false)
                    .descriptorSetLayout(ctx.textureSetLayout())
                    .pushConstants(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4))
                    .build(ctx);

    // Static index buffer: every quad is two triangles (0,1,2) (2,3,0).
    std::vector<uint32_t> indices(size_t(maxSprites) * 6);
    for (uint32_t i = 0; i < maxSprites; ++i) {
        const uint32_t v = i * 4;
        uint32_t* idx = &indices[size_t(i) * 6];
        idx[0] = v + 0; idx[1] = v + 1; idx[2] = v + 2;
        idx[3] = v + 2; idx[4] = v + 3; idx[5] = v + 0;
    }
    indexBuffer_ = gfx::Buffer::createWithData(ctx, indices.data(), indices.size() * sizeof(uint32_t),
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "sprite indices");

    // Dynamic vertex data: one host-visible buffer per frame in flight so the
    // CPU never overwrites vertices the GPU is still reading.
    for (uint32_t i = 0; i < renderer.framesInFlight(); ++i)
        vertexBuffers_.emplace_back(ctx, VkDeviceSize(maxSprites) * 4 * sizeof(Vertex),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gfx::MemoryUsage::CpuToGpu,
                                    "sprite vertices");

    white_ = gfx::Texture::solid(ctx, 255, 255, 255);
}

void SpriteBatch::begin(const gfx::FrameContext& frame, const glm::mat4& viewProjection)
{
    if (active_)
        core::fatal("SpriteBatch::begin called twice without end()");
    active_ = true;
    cmd_ = frame.cmd;

    if (frame.frameNumber != frameNumber_) {
        // First batch of a new frame: start writing at the beginning of this
        // frame's vertex buffer.
        frameNumber_ = frame.frameNumber;
        spriteCount_ = 0;
        stats_ = {};
        warnedFull_ = false;
    }
    batchStart_ = spriteCount_;
    batchBegin_ = spriteCount_;
    batchTexture_ = nullptr;
    frameIndex_ = frame.frameIndex;

    gfx::Buffer& vb = vertexBuffers_[frame.frameIndex];
    vertices_ = static_cast<Vertex*>(vb.mapped());

    pipeline_.bind(cmd_);
    const VkDeviceSize offset = 0;
    const VkBuffer vbHandle = vb.handle();
    vkCmdBindVertexBuffers(cmd_, 0, 1, &vbHandle, &offset);
    vkCmdBindIndexBuffer(cmd_, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(cmd_, pipeline_.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4),
                       &viewProjection);
}

void SpriteBatch::pushQuad(const gfx::Texture& texture, const glm::vec2 corners[4], const UvRect& uv,
                           Color color)
{
    if (!active_)
        core::fatal("SpriteBatch::draw called outside begin()/end()");

    if (spriteCount_ >= maxSprites_) {
        if (!warnedFull_) {
            LOG_WARN("SpriteBatch full (%u sprites); increase maxSpritesPerFrame", maxSprites_);
            warnedFull_ = true;
        }
        return;
    }

    if (batchTexture_ != &texture) {
        flush();
        batchTexture_ = &texture;
    }

    const uint32_t packed = packColor(color);
    Vertex* v = vertices_ + size_t(spriteCount_) * 4;
    v[0] = {corners[0], {uv.min.x, uv.min.y}, packed}; // top-left
    v[1] = {corners[1], {uv.max.x, uv.min.y}, packed}; // top-right
    v[2] = {corners[2], {uv.max.x, uv.max.y}, packed}; // bottom-right
    v[3] = {corners[3], {uv.min.x, uv.max.y}, packed}; // bottom-left
    ++spriteCount_;
}

void SpriteBatch::draw(const gfx::Texture& texture, const Sprite& s)
{
    // Corners relative to the pivot, then rotate and translate.
    const glm::vec2 tl = -s.origin * s.size;
    const glm::vec2 local[4] = {tl, tl + glm::vec2(s.size.x, 0.0f), tl + s.size, tl + glm::vec2(0.0f, s.size.y)};

    glm::vec2 corners[4];
    if (s.rotation != 0.0f) {
        const float c = std::cos(s.rotation), sn = std::sin(s.rotation);
        for (int i = 0; i < 4; ++i)
            corners[i] = s.position + glm::vec2(local[i].x * c - local[i].y * sn, local[i].x * sn + local[i].y * c);
    } else {
        for (int i = 0; i < 4; ++i)
            corners[i] = s.position + local[i];
    }
    pushQuad(texture, corners, s.uv, s.color);
}

void SpriteBatch::draw(const gfx::Texture& texture, glm::vec2 position, float scale, float rotation,
                       Color color)
{
    Sprite s;
    s.position = position;
    s.size = texture.size() * scale;
    s.rotation = rotation;
    s.color = color;
    draw(texture, s);
}

void SpriteBatch::drawRect(glm::vec2 position, glm::vec2 size, Color color)
{
    const glm::vec2 corners[4] = {position, position + glm::vec2(size.x, 0.0f), position + size,
                                  position + glm::vec2(0.0f, size.y)};
    pushQuad(white_, corners, UvRect{}, color);
}

void SpriteBatch::drawRectOutline(glm::vec2 p, glm::vec2 size, float t, Color color)
{
    drawRect(p, {size.x, t}, color);
    drawRect({p.x, p.y + size.y - t}, {size.x, t}, color);
    drawRect({p.x, p.y + t}, {t, size.y - 2.0f * t}, color);
    drawRect({p.x + size.x - t, p.y + t}, {t, size.y - 2.0f * t}, color);
}

void SpriteBatch::drawLine(glm::vec2 a, glm::vec2 b, float thickness, Color color)
{
    const glm::vec2 d = b - a;
    const float len = glm::length(d);
    if (len <= 0.0f)
        return;
    const glm::vec2 n = glm::vec2(-d.y, d.x) / len * (thickness * 0.5f);
    const glm::vec2 corners[4] = {a + n, b + n, b - n, a - n};
    pushQuad(white_, corners, UvRect{}, color);
}

glm::vec2 SpriteBatch::drawText(const Font& font, std::string_view text, glm::vec2 position, const TextStyle& style)
{
    const float scale = style.size / font.bakedSize(); // baked pixels -> world units
    const float lineStep = font.lineHeight(style.size) * style.lineSpacing;

    glm::vec2 origin = position;
    if (style.snapToPixel)
        origin = glm::round(origin);

    size_t lineStart = 0;
    int line = 0;
    for (;;) {
        // Horizontal start of this line, depending on alignment.
        float x = origin.x;
        if (style.align != TextAlign::Left) {
            const float width = font.lineWidth(text, lineStart, style.size);
            x -= style.align == TextAlign::Center ? width * 0.5f : width;
            if (style.snapToPixel)
                x = std::round(x);
        }
        float baseline = origin.y + font.ascent(style.size) + float(line) * lineStep;
        if (style.snapToPixel)
            baseline = std::round(baseline);

        size_t i = lineStart;
        uint32_t prev = 0;
        bool newline = false;
        while (i < text.size()) {
            const uint32_t cp = decodeUtf8(text, i);
            if (cp == '\n') {
                newline = true;
                break;
            }
            if (prev)
                x += font.kerning(prev, cp) * scale;
            const Font::Glyph& g = font.glyph(cp);
            if (g.visible) {
                const glm::vec2 p0(x + g.offMin.x * scale, baseline + g.offMin.y * scale);
                const glm::vec2 p1(x + g.offMax.x * scale, baseline + g.offMax.y * scale);
                const glm::vec2 corners[4] = {p0, {p1.x, p0.y}, p1, {p0.x, p1.y}};
                pushQuad(font.texture(), corners, UvRect{g.uvMin, g.uvMax}, style.color);
            }
            x += g.advance * scale;
            prev = cp;
        }
        if (!newline)
            break;
        lineStart = i; // just past the '\n'
        ++line;
    }

    glm::vec2 size = font.measure(text, style.size);
    size.y += float(line) * (lineStep - font.lineHeight(style.size)); // account for lineSpacing
    return size;
}

void SpriteBatch::flush()
{
    const uint32_t count = spriteCount_ - batchStart_;
    if (count == 0 || !batchTexture_)
        return;

    const VkDescriptorSet set = batchTexture_->descriptorSet();
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.layout(), 0, 1, &set, 0, nullptr);
    vkCmdDrawIndexed(cmd_, count * 6, 1, batchStart_ * 6, 0, 0);

    stats_.sprites += count;
    stats_.drawCalls += 1;
    batchStart_ = spriteCount_;
}

void SpriteBatch::end()
{
    if (!active_)
        core::fatal("SpriteBatch::end called without begin()");
    flush();
    // Make the CPU writes visible to the GPU. A no-op on host-coherent memory
    // (practically always the case), required on the rest.
    gfx::Buffer& vb = vertexBuffers_[frameIndex_];
    const VkDeviceSize vertexBytes = 4 * sizeof(Vertex);
    vb.flush(VkDeviceSize(batchBegin_) * vertexBytes, VkDeviceSize(spriteCount_ - batchBegin_) * vertexBytes);
    active_ = false;
    batchTexture_ = nullptr;
}

} // namespace render2d
