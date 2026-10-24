#pragma once

#include "gfx/Buffer.h"
#include "gfx/Pipeline.h"
#include "gfx/Renderer.h"
#include "gfx/Texture.h"
#include "render2d/Font.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <string_view>
#include <vector>

namespace render2d {

// Colours are sRGB (as picked in any image editor), 0..1.
using Color = glm::vec4;
namespace colors {
inline constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color Black{0.0f, 0.0f, 0.0f, 1.0f};
inline constexpr Color Red{1.0f, 0.2f, 0.2f, 1.0f};
inline constexpr Color Green{0.2f, 1.0f, 0.3f, 1.0f};
inline constexpr Color Blue{0.25f, 0.45f, 1.0f, 1.0f};
inline constexpr Color Yellow{1.0f, 0.9f, 0.2f, 1.0f};
} // namespace colors

// Normalised texture coordinates of a sub-rectangle (atlas frame).
struct UvRect {
    glm::vec2 min{0.0f, 0.0f};
    glm::vec2 max{1.0f, 1.0f};

    // Build from pixel coordinates inside a texture (e.g. sprite sheet frames).
    static UvRect fromPixels(const gfx::Texture& tex, float x, float y, float w, float h)
    {
        const glm::vec2 size = tex.size();
        return {glm::vec2(x, y) / size, glm::vec2(x + w, y + h) / size};
    }
    UvRect flippedX() const { return {{max.x, min.y}, {min.x, max.y}}; }
    UvRect flippedY() const { return {{min.x, max.y}, {max.x, min.y}}; }
};

enum class TextAlign { Left, Center, Right };

struct TextStyle {
    float size = 24.0f;            // line height-ish size in world units (HUD camera: pixels)
    Color color = colors::White;
    TextAlign align = TextAlign::Left;
    float lineSpacing = 1.0f;      // multiplier for the distance between lines
    bool snapToPixel = false;      // round the start position; crisper text with a 1:1 HUD camera
};

struct Sprite {
    glm::vec2 position{0.0f};   // world position of the origin point
    glm::vec2 size{32.0f};      // world size
    glm::vec2 origin{0.5f};     // pivot, normalised (0,0 = top-left, 0.5 = centre)
    float rotation = 0.0f;      // radians, clockwise on screen
    Color color = colors::White;
    UvRect uv{};
};

// Batches textured quads into as few draw calls as possible. Consecutive
// sprites that share a texture become one draw call; draw order is preserved
// (later sprites are drawn on top), so group by texture/atlas where you can.
//
//   batch.begin(frame, camera.viewProjection());
//   batch.draw(texture, sprite);
//   batch.drawRect({0, 0}, {100, 20}, colors::Red);
//   batch.drawText(font, "Hello", {10, 10}, {.size = 32});
//   batch.end();
class SpriteBatch {
public:
    SpriteBatch(gfx::VulkanContext& ctx, gfx::Renderer& renderer, uint32_t maxSpritesPerFrame = 20000);

    void begin(const gfx::FrameContext& frame, const glm::mat4& viewProjection);
    void draw(const gfx::Texture& texture, const Sprite& sprite);
    // Convenience: texture drawn at its pixel size (times scale), centred.
    void draw(const gfx::Texture& texture, glm::vec2 position, float scale = 1.0f,
              float rotation = 0.0f, Color color = colors::White);
    // Solid rectangle; position is the top-left corner.
    void drawRect(glm::vec2 position, glm::vec2 size, Color color);
    void drawRectOutline(glm::vec2 position, glm::vec2 size, float thickness, Color color);
    void drawLine(glm::vec2 a, glm::vec2 b, float thickness, Color color);
    // UTF-8 text; '\n' starts a new line. `position` is the top of the first
    // line at its left edge, centre or right edge, depending on style.align.
    // Returns the size of the drawn block (same as font.measure()).
    glm::vec2 drawText(const Font& font, std::string_view utf8, glm::vec2 position, const TextStyle& style = {});
    void end();
    // begin()/end() may be used several times per frame, e.g. once for the
    // world camera and once more for a screen-space HUD.

    struct Stats {
        uint32_t sprites = 0;
        uint32_t drawCalls = 0;
    };
    const Stats& stats() const { return stats_; } // for the current/last frame

    const gfx::Texture& whiteTexture() const { return white_; }

private:
    struct Vertex {
        glm::vec2 position;
        glm::vec2 uv;
        uint32_t color; // packed RGBA8
    };

    void flush();
    void pushQuad(const gfx::Texture& texture, const glm::vec2 corners[4], const UvRect& uv, Color color);

    uint32_t maxSprites_;
    gfx::Pipeline pipeline_;
    gfx::Buffer indexBuffer_;
    std::vector<gfx::Buffer> vertexBuffers_; // one per frame in flight
    gfx::Texture white_;

    // Per-frame state
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    Vertex* vertices_ = nullptr;
    uint32_t spriteCount_ = 0;      // sprites written this frame (across begin/end pairs)
    uint32_t batchStart_ = 0;       // first sprite of the pending batch
    uint32_t batchBegin_ = 0;       // first sprite written since begin()
    uint32_t frameIndex_ = 0;
    const gfx::Texture* batchTexture_ = nullptr;
    uint64_t frameNumber_ = ~0ull;
    bool active_ = false;
    bool warnedFull_ = false;
    Stats stats_;
};

} // namespace render2d
