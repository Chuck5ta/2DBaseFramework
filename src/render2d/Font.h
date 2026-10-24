#pragma once

#include "gfx/Texture.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct stbtt_fontinfo;

namespace render2d {

struct FontDesc {
    // Size the glyphs are rasterised at, in pixels. Text stays sharpest when drawn
    // at (or a little below) this size, so bake at the largest size you use.
    float pixelHeight = 48.0f;
    // Characters to include (a contiguous Unicode range). The default covers
    // printable ASCII and Latin-1: accented letters such as é, ü, ñ and symbols
    // such as £ and ©. (€ is U+20AC, outside this range.)
    uint32_t firstCodepoint = 32;
    uint32_t lastCodepoint = 255;
    // Horizontal oversampling gives smoother sub-pixel positioning. 1 = off.
    int oversample = 2;
    // true: linear filtering + mipmaps (smooth fonts).
    // false: nearest filtering (pixel fonts drawn at whole multiples of their size).
    bool smooth = true;
};

// A TrueType/OpenType font baked into a texture atlas with stb_truetype.
// Draw text with SpriteBatch::drawText. Move-only.
//
// All metrics take the size the text is drawn at, in world units (for a HUD
// camera that is screen pixels).
class Font {
public:
    struct Glyph {
        glm::vec2 uvMin{0.0f}, uvMax{0.0f};   // position in the atlas (0..1)
        glm::vec2 offMin{0.0f}, offMax{0.0f}; // quad corners relative to the pen, baked pixels, +Y down
        float advance = 0.0f;                 // pen movement, baked pixels
        bool visible = false;                 // false for spaces
    };

    Font();
    ~Font();
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // Loads a .ttf/.otf file. Aborts with a message if it can't be read.
    static Font load(gfx::VulkanContext& ctx, const std::string& path, const FontDesc& desc = {});
    static Font fromMemory(gfx::VulkanContext& ctx, std::vector<uint8_t> fontData, const FontDesc& desc = {},
                           const char* debugName = "font");

    bool valid() const { return texture_.valid(); }
    float bakedSize() const { return desc_.pixelHeight; }
    const gfx::Texture& texture() const { return texture_; }

    // Distance from the top of a line to its baseline.
    float ascent(float size) const { return ascent_ * size / desc_.pixelHeight; }
    // Distance between the baselines of two lines.
    float lineHeight(float size) const { return (ascent_ - descent_ + lineGap_) * size / desc_.pixelHeight; }

    // Width of the widest line and total height of `utf8` drawn at `size`.
    glm::vec2 measure(std::string_view utf8, float size) const;
    // Width of a single line (stops at '\n' or the end), starting at byte `start`.
    float lineWidth(std::string_view utf8, size_t start, float size) const;

    // Glyph for a codepoint; characters that weren't baked map to '?'.
    const Glyph& glyph(uint32_t codepoint) const;
    // Extra spacing between a pair of characters (negative pulls them together), baked pixels.
    float kerning(uint32_t left, uint32_t right) const;

private:
    FontDesc desc_;
    std::vector<uint8_t> data_;                 // the font file; stb_truetype reads it for kerning
    std::unique_ptr<stbtt_fontinfo> info_;
    std::vector<Glyph> glyphs_;                 // index = codepoint - firstCodepoint
    float ascent_ = 0.0f, descent_ = 0.0f, lineGap_ = 0.0f; // baked pixels (descent is negative)
    gfx::Texture texture_;
};

// Decodes one UTF-8 character starting at `i` and advances `i` past it.
// Invalid bytes decode as U+FFFD.
uint32_t decodeUtf8(std::string_view text, size_t& i);

} // namespace render2d
