#include "render2d/Animation.h"

#include "core/Log.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace render2d {

namespace {

constexpr float kMinFrameSeconds = 0.001f;
constexpr float kFrameTolerance = 1e-5f; // seconds

const SheetFrame& emptyFrame()
{
    static const SheetFrame frame{};
    return frame;
}

const std::string& emptyName()
{
    static const std::string name;
    return name;
}

std::string directoryOf(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// Aseprite writes "repeat" as a string ("1"); accept a number too.
int repeatCount(const nlohmann::ordered_json& tag)
{
    const auto it = tag.find("repeat");
    if (it == tag.end())
        return 0;
    if (it->is_number_integer())
        return it->get<int>();
    if (it->is_string())
        return std::atoi(it->get<std::string>().c_str());
    return 0;
}

} // namespace

// --- AnimationClip ------------------------------------------------------------

AnimationClip& AnimationClip::addEvent(int frame, std::string eventName)
{
    events.push_back({frame, std::move(eventName)});
    return *this;
}

float AnimationClip::length() const
{
    float total = 0.0f;
    for (float d : durations)
        total += d;
    return total;
}

// --- SpriteSheet --------------------------------------------------------------

gfx::TextureDesc SpriteSheet::pixelArt()
{
    gfx::TextureDesc desc;
    desc.filter = VK_FILTER_NEAREST;
    desc.mipmaps = false;
    desc.wrap = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return desc;
}

SpriteSheet SpriteSheet::fromGrid(glm::ivec2 imageSize, glm::ivec2 frameSize, int frameCount)
{
    SpriteSheet sheet;
    if (frameSize.x <= 0 || frameSize.y <= 0 || imageSize.x < frameSize.x || imageSize.y < frameSize.y) {
        LOG_WARN("SpriteSheet: %dx%d frames don't fit a %dx%d image", frameSize.x, frameSize.y, imageSize.x,
                 imageSize.y);
        return sheet;
    }
    const int columns = imageSize.x / frameSize.x;
    const int rows = imageSize.y / frameSize.y;
    int count = columns * rows;
    if (frameCount > 0)
        count = std::min(count, frameCount);
    const glm::vec2 image(imageSize);
    for (int i = 0; i < count; ++i) {
        const glm::vec2 topLeft(float((i % columns) * frameSize.x), float((i / columns) * frameSize.y));
        SheetFrame f;
        f.uv = {topLeft / image, (topLeft + glm::vec2(frameSize)) / image};
        f.size = glm::vec2(frameSize);
        sheet.frames_.push_back(f);
    }
    return sheet;
}

SpriteSheet SpriteSheet::loadGrid(gfx::VulkanContext& gpu, const std::string& imagePath, glm::ivec2 frameSize,
                                  int frameCount, const gfx::TextureDesc& desc)
{
    gfx::Texture texture = gfx::Texture::fromFile(gpu, imagePath, desc);
    SpriteSheet sheet = fromGrid({int(texture.width()), int(texture.height())}, frameSize, frameCount);
    sheet.setTexture(std::move(texture));
    return sheet;
}

SpriteSheet SpriteSheet::fromAsepriteJson(std::string_view text, std::string* imageFile, std::string* error)
{
    using Json = nlohmann::ordered_json; // keeps "Hash" exports in file order
    SpriteSheet sheet;
    auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return SpriteSheet{};
    };

    const Json j = Json::parse(text.begin(), text.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return fail("not valid JSON");
    const auto framesIt = j.find("frames");
    const auto metaIt = j.find("meta");
    if (framesIt == j.end() || metaIt == j.end() || !metaIt->contains("size"))
        return fail("not an Aseprite sprite-sheet export (no \"frames\" or \"meta\")");

    const Json& size = (*metaIt)["size"];
    const glm::vec2 image(size.value("w", 0.0f), size.value("h", 0.0f));
    if (image.x <= 0.0f || image.y <= 0.0f)
        return fail("meta.size is missing");

    // "Array" exports list frames in order; "Hash" exports key them by file name.
    std::vector<const Json*> frames;
    if (framesIt->is_array())
        for (const Json& f : *framesIt)
            frames.push_back(&f);
    else if (framesIt->is_object())
        for (const auto& item : framesIt->items())
            frames.push_back(&item.value());
    bool trimmedWarned = false;
    for (const Json* f : frames) {
        const Json& r = f->value("frame", Json::object());
        const glm::vec2 topLeft(r.value("x", 0.0f), r.value("y", 0.0f));
        const glm::vec2 frameSize(r.value("w", 0.0f), r.value("h", 0.0f));
        if (f->value("rotated", false))
            return fail("rotated frames aren't supported (untick \"Rotate\" when exporting)");
        if (f->value("trimmed", false) && !trimmedWarned) {
            LOG_WARN("SpriteSheet: trimmed Aseprite frames will jump around; export without \"Trim\"");
            trimmedWarned = true;
        }
        SheetFrame frame;
        frame.uv = {topLeft / image, (topLeft + frameSize) / image};
        frame.size = frameSize;
        frame.duration = std::max(f->value("duration", 100.0f) / 1000.0f, kMinFrameSeconds);
        sheet.frames_.push_back(frame);
    }
    if (sheet.frames_.empty())
        return fail("the export has no frames");

    const int last = sheet.frameCount() - 1;
    for (const Json& tag : metaIt->value("frameTags", Json::array())) {
        AnimationClip clip;
        clip.name = tag.value("name", std::string());
        const int from = std::clamp(tag.value("from", 0), 0, last);
        const int to = std::clamp(tag.value("to", 0), from, last);
        for (int i = from; i <= to; ++i)
            clip.frames.push_back(i);
        const std::string direction = tag.value("direction", std::string("forward"));
        if (direction == "reverse" || direction == "pingpong_reverse")
            std::reverse(clip.frames.begin(), clip.frames.end());
        if (direction == "pingpong" || direction == "pingpong_reverse")
            clip.loop = AnimationLoop::PingPong;
        else if (repeatCount(tag) == 1)
            clip.loop = AnimationLoop::Once;
        for (int index : clip.frames)
            clip.durations.push_back(sheet.frames_[size_t(index)].duration);
        if (!clip.name.empty())
            sheet.addClip(std::move(clip));
    }
    if (sheet.clips_.empty()) { // no tags: one clip of everything
        AnimationClip all;
        all.name = "default";
        for (int i = 0; i <= last; ++i) {
            all.frames.push_back(i);
            all.durations.push_back(sheet.frames_[size_t(i)].duration);
        }
        sheet.addClip(std::move(all));
    }
    if (imageFile)
        *imageFile = metaIt->value("image", std::string());
    return sheet;
}

SpriteSheet SpriteSheet::loadAseprite(gfx::VulkanContext& gpu, const std::string& jsonPath, const gfx::TextureDesc& desc)
{
    size_t bytes = 0;
    void* data = SDL_LoadFile(jsonPath.c_str(), &bytes);
    if (!data)
        core::fatal("Could not open sprite sheet '%s': %s", jsonPath.c_str(), SDL_GetError());
    const std::string text(static_cast<const char*>(data), bytes);
    SDL_free(data);

    std::string image, error;
    SpriteSheet sheet = fromAsepriteJson(text, &image, &error);
    if (!sheet.valid())
        core::fatal("Sprite sheet '%s': %s", jsonPath.c_str(), error.c_str());
    if (image.empty())
        core::fatal("Sprite sheet '%s' doesn't name its image (meta.image)", jsonPath.c_str());
    sheet.setTexture(gfx::Texture::fromFile(gpu, directoryOf(jsonPath) + image, desc));
    return sheet;
}

const SheetFrame& SpriteSheet::frame(int index) const
{
    if (frames_.empty())
        return emptyFrame();
    return frames_[size_t(std::clamp(index, 0, frameCount() - 1))];
}

AnimationClip& SpriteSheet::addClip(const std::string& name, int first, int count, float framesPerSecond,
                                    AnimationLoop loop)
{
    AnimationClip clip;
    clip.name = name;
    clip.loop = loop;
    const float duration = framesPerSecond > 0.0f ? 1.0f / framesPerSecond : 0.1f;
    for (int i = 0; i < count; ++i) {
        clip.frames.push_back(std::clamp(first + i, 0, std::max(frameCount() - 1, 0)));
        clip.durations.push_back(duration);
    }
    return addClip(std::move(clip));
}

AnimationClip& SpriteSheet::addClip(AnimationClip clip)
{
    clip.durations.resize(clip.frames.size(), 0.1f);
    for (float& d : clip.durations)
        d = std::max(d, kMinFrameSeconds);
    std::string name = clip.name;
    AnimationClip& stored = clips_[name];
    stored = std::move(clip);
    return stored;
}

const AnimationClip* SpriteSheet::clip(std::string_view name) const
{
    const auto it = clips_.find(name);
    if (it != clips_.end())
        return &it->second;
    if (warned_.find(name) == warned_.end()) {
        warned_.emplace(name);
        LOG_WARN("SpriteSheet: no clip called '%.*s'", int(name.size()), name.data());
    }
    return nullptr;
}

AnimationClip* SpriteSheet::clip(std::string_view name)
{
    return const_cast<AnimationClip*>(std::as_const(*this).clip(name));
}

std::vector<std::string> SpriteSheet::clipNames() const
{
    std::vector<std::string> names;
    for (const auto& [name, clip] : clips_)
        names.push_back(name);
    return names;
}

// --- Animator -----------------------------------------------------------------

void Animator::play(const SpriteSheet& sheet, std::string_view name, bool restart)
{
    const AnimationClip* next = sheet.clip(name);
    if (!next || next->frames.empty())
        return;
    if (next == clip_ && &sheet == sheet_ && !restart)
        return; // already playing: keep going
    sheet_ = &sheet;
    clip_ = next;
    direction_ = 1;
    frameTime_ = 0.0f;
    elapsed_ = 0.0f;
    finished_ = false;
    pending_.clear();
    index_ = 0;
    for (const AnimationEvent& e : clip_->events)
        if (e.frame == 0)
            pending_.push_back(e.name); // delivered by the next update()
}

void Animator::stop()
{
    sheet_ = nullptr;
    clip_ = nullptr;
    finished_ = false;
    pending_.clear();
    events_.clear();
}

void Animator::enterFrame(int index)
{
    index_ = index;
    for (const AnimationEvent& e : clip_->events)
        if (e.frame == index)
            events_.push_back(e.name);
}

int Animator::nextIndex(bool& wrapped) const
{
    const int count = int(clip_->frames.size());
    wrapped = false;
    switch (clip_->loop) {
    case AnimationLoop::Once:
        return index_ + 1 < count ? index_ + 1 : -1;
    case AnimationLoop::PingPong: {
        if (count == 1)
            return 0;
        int next = index_ + direction_;
        if (next < 0 || next >= count) {
            wrapped = next < 0; // a full there-and-back
            next = index_ - direction_;
        }
        return next;
    }
    case AnimationLoop::Loop:
    default:
        if (index_ + 1 < count)
            return index_ + 1;
        wrapped = true;
        return 0;
    }
}

void Animator::update(float dt)
{
    events_.swap(pending_);
    pending_.clear();
    if (!clip_ || finished_)
        return;
    const float step = dt * std::max(speed, 0.0f);
    if (step <= 0.0f)
        return;
    elapsed_ += step;
    frameTime_ += step;

    // Several frames can pass in one update (a long frame, or high speed).
    for (int guard = 0; guard < 10000; ++guard) {
        const float duration = clip_->durations[size_t(index_)];
        // A tiny tolerance, because sums of steps fall just short in floating point:
        // six 1/60 s steps add up to 0.0999999, and a 0.1 s frame should end there.
        if (frameTime_ < duration - kFrameTolerance)
            break;
        frameTime_ -= duration;
        bool wrapped = false;
        const int next = nextIndex(wrapped);
        if (next < 0) { // a Once clip ended: hold the last frame
            finished_ = true;
            frameTime_ = duration;
            break;
        }
        if (clip_->loop == AnimationLoop::PingPong && next == index_ - direction_)
            direction_ = -direction_;
        if (wrapped)
            elapsed_ = frameTime_;
        enterFrame(next);
    }
}

bool Animator::isPlaying(std::string_view name) const
{
    return clip_ && clip_->name == name;
}

const std::string& Animator::clipName() const
{
    return clip_ ? clip_->name : emptyName();
}

UvRect Animator::uv(bool flipX) const
{
    if (!clip_)
        return {};
    const UvRect r = sheet_->frame(clip_->frames[size_t(index_)]).uv;
    return flipX ? r.flippedX() : r;
}

glm::vec2 Animator::frameSize() const
{
    return clip_ ? sheet_->frame(clip_->frames[size_t(index_)]).size : glm::vec2(0.0f);
}

bool Animator::eventFired(std::string_view name) const
{
    return std::find(events_.begin(), events_.end(), name) != events_.end();
}

} // namespace render2d
