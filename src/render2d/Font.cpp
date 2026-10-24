#include "render2d/Font.h"

#include "core/Log.h"

#include <SDL3/SDL_iostream.h>
#include <stb_truetype.h>

#include <algorithm>
#include <utility>

namespace render2d {

uint32_t decodeUtf8(std::string_view s, size_t& i)
{
    const auto byte = [&](size_t k) { return static_cast<uint8_t>(s[k]); };
    const uint8_t c = byte(i);

    int extra = 0;
    uint32_t cp = 0;
    if (c < 0x80)      { cp = c;        extra = 0; } // ASCII
    else if (c < 0xC0) { ++i; return 0xFFFD; }       // stray continuation byte
    else if (c < 0xE0) { cp = c & 0x1F; extra = 1; }
    else if (c < 0xF0) { cp = c & 0x0F; extra = 2; }
    else if (c < 0xF8) { cp = c & 0x07; extra = 3; }
    else               { ++i; return 0xFFFD; }       // not valid UTF-8

    ++i;
    for (int k = 0; k < extra; ++k, ++i) {
        if (i >= s.size() || (byte(i) & 0xC0) != 0x80)
            return 0xFFFD; // truncated sequence
        cp = (cp << 6) | (byte(i) & 0x3F);
    }
    return cp;
}

Font::Font() = default;
Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

Font Font::load(gfx::VulkanContext& ctx, const std::string& path, const FontDesc& desc)
{
    size_t size = 0;
    void* file = SDL_LoadFile(path.c_str(), &size);
    if (!file)
        core::fatal("Failed to load font '%s': %s", path.c_str(), SDL_GetError());
    std::vector<uint8_t> data(static_cast<uint8_t*>(file), static_cast<uint8_t*>(file) + size);
    SDL_free(file);
    return fromMemory(ctx, std::move(data), desc, path.c_str());
}

Font Font::fromMemory(gfx::VulkanContext& ctx, std::vector<uint8_t> fontData, const FontDesc& desc,
                      const char* debugName)
{
    Font font;
    font.desc_ = desc;
    font.desc_.oversample = std::clamp(desc.oversample, 1, 8);
    font.data_ = std::move(fontData);
    font.info_ = std::make_unique<stbtt_fontinfo>();

    const unsigned char* data = font.data_.data();
    if (!stbtt_InitFont(font.info_.get(), data, stbtt_GetFontOffsetForIndex(data, 0)))
        core::fatal("'%s' is not a valid TrueType/OpenType font", debugName);

    // Vertical metrics, converted to baked pixels.
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(font.info_.get(), &ascent, &descent, &lineGap);
    const float scale = stbtt_ScaleForPixelHeight(font.info_.get(), desc.pixelHeight);
    font.ascent_ = float(ascent) * scale;
    font.descent_ = float(descent) * scale;
    font.lineGap_ = float(lineGap) * scale;

    // Pack all glyphs into the smallest square atlas that fits (256 .. 4096).
    const uint32_t first = desc.firstCodepoint;
    const int count = int(desc.lastCodepoint - desc.firstCodepoint + 1);
    std::vector<stbtt_packedchar> packed(static_cast<size_t>(count));
    std::vector<uint8_t> coverage;
    int atlasSize = 256;
    for (;; atlasSize *= 2) {
        if (atlasSize > 4096)
            core::fatal("Font '%s' at %.0f px with %d glyphs doesn't fit a 4096x4096 atlas", debugName,
                        double(desc.pixelHeight), count);
        coverage.assign(size_t(atlasSize) * size_t(atlasSize), 0);

        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, coverage.data(), atlasSize, atlasSize, 0, 1, nullptr))
            core::fatal("stbtt_PackBegin failed");
        stbtt_PackSetOversampling(&pc, unsigned(font.desc_.oversample), 1);
        const int ok = stbtt_PackFontRange(&pc, data, 0, desc.pixelHeight, int(first), count, packed.data());
        stbtt_PackEnd(&pc);
        if (ok)
            break;
    }

    // Glyph table.
    font.glyphs_.resize(size_t(count));
    const float inv = 1.0f / float(atlasSize);
    for (int i = 0; i < count; ++i) {
        const stbtt_packedchar& p = packed[size_t(i)];
        Glyph& g = font.glyphs_[size_t(i)];
        g.uvMin = {float(p.x0) * inv, float(p.y0) * inv};
        g.uvMax = {float(p.x1) * inv, float(p.y1) * inv};
        g.offMin = {p.xoff, p.yoff};
        g.offMax = {p.xoff2, p.yoff2};
        g.advance = p.xadvance;
        g.visible = p.x1 > p.x0 && p.y1 > p.y0;
    }

    // Upload: white RGB with coverage in alpha, so the regular sprite shader can
    // draw (and tint) text without a special pipeline.
    std::vector<uint8_t> rgba(coverage.size() * 4);
    for (size_t i = 0; i < coverage.size(); ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = coverage[i];
    }
    gfx::TextureDesc texDesc;
    texDesc.filter = desc.smooth ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    texDesc.mipmaps = desc.smooth; // lets text drawn well below the baked size stay smooth
    texDesc.wrap = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    texDesc.debugName = debugName;
    font.texture_ = gfx::Texture::fromPixels(ctx, uint32_t(atlasSize), uint32_t(atlasSize), rgba.data(), texDesc);

    LOG_DEBUG("Font '%s': %d glyphs at %.0f px in a %dx%d atlas", debugName, count, double(desc.pixelHeight),
              atlasSize, atlasSize);
    return font;
}

const Font::Glyph& Font::glyph(uint32_t cp) const
{
    if (cp >= desc_.firstCodepoint && cp <= desc_.lastCodepoint)
        return glyphs_[cp - desc_.firstCodepoint];
    const uint32_t fallback = '?';
    if (fallback >= desc_.firstCodepoint && fallback <= desc_.lastCodepoint)
        return glyphs_[fallback - desc_.firstCodepoint];
    return glyphs_.front();
}

float Font::kerning(uint32_t left, uint32_t right) const
{
    if (!info_)
        return 0.0f;
    const float scale = stbtt_ScaleForPixelHeight(info_.get(), desc_.pixelHeight);
    return float(stbtt_GetCodepointKernAdvance(info_.get(), int(left), int(right))) * scale;
}

float Font::lineWidth(std::string_view s, size_t start, float size) const
{
    float width = 0.0f;
    uint32_t prev = 0;
    for (size_t i = start; i < s.size();) {
        const uint32_t cp = decodeUtf8(s, i);
        if (cp == '\n')
            break;
        if (prev)
            width += kerning(prev, cp);
        width += glyph(cp).advance;
        prev = cp;
    }
    return width * size / desc_.pixelHeight;
}

glm::vec2 Font::measure(std::string_view s, float size) const
{
    float widest = 0.0f;
    int lines = 1;
    size_t lineStart = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '\n') {
            widest = std::max(widest, lineWidth(s, lineStart, size));
            if (i < s.size())
                ++lines;
            lineStart = i + 1;
        }
    }
    // Height: every line's spacing except the last, plus the last line's ascent + descent.
    const float height = float(lines - 1) * lineHeight(size) + (ascent_ - descent_) * size / desc_.pixelHeight;
    return {widest, height};
}

} // namespace render2d
