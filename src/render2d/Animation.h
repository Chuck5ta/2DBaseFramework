#pragma once

// Sprite-sheet animation: frames in a texture, named clips, and an Animator that
// plays them.
//
//   // A sheet exported from Aseprite (File > Export Sprite Sheet, JSON data):
//   // its tags become clips, with each frame's own duration.
//   sheet_ = render2d::SpriteSheet::loadAseprite(app.gpu(), core::assetPath("sprites/hero.json"));
//   // ...or a plain grid of equal frames, with clips added in code:
//   coins_ = render2d::SpriteSheet::loadGrid(app.gpu(), core::assetPath("sprites/coin.png"), {16, 16});
//   coins_.addClip("spin", 0, 8, 12.0f);
//
//   animator_.play(sheet_, "walk");   // safe to call every frame: keeps playing
//   animator_.update(dt);             // in onFixedUpdate for gameplay timing
//   if (animator_.eventFired("hit")) ...
//   sprite.uv = animator_.uv(facingLeft);
//   batch.draw(sheet_.texture(), sprite);

#include "gfx/Texture.h"
#include "render2d/SpriteBatch.h"

#include <glm/vec2.hpp>

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace gfx { class VulkanContext; }

namespace render2d {

enum class AnimationLoop {
    Loop,     // 0 1 2 3 0 1 2 3 ...
    Once,     // 0 1 2 3, then stays on 3 (Animator::finished() becomes true)
    PingPong, // 0 1 2 3 2 1 0 1 ...
};

// A named moment in a clip, e.g. "footstep" or "hit". It fires when its frame starts.
struct AnimationEvent {
    int frame = 0; // index within the clip
    std::string name;
};

struct AnimationClip {
    std::string name;
    std::vector<int> frames;      // indices into the sheet's frames
    std::vector<float> durations; // seconds per frame (same length as frames)
    AnimationLoop loop = AnimationLoop::Loop;
    std::vector<AnimationEvent> events;

    // Adds an event; returns *this so calls can be chained.
    AnimationClip& addEvent(int frame, std::string eventName);
    float length() const; // seconds for one pass
};

// One frame of a sheet: where it is in the texture.
struct SheetFrame {
    UvRect uv;
    glm::vec2 size{0.0f};       // pixels
    float duration = 0.1f;      // default seconds (Aseprite's per-frame duration)
};

class SpriteSheet {
public:
    SpriteSheet() = default;

    // Loads an image and splits it into frames of `frameSize` pixels, numbered left to
    // right, top to bottom. `frameCount` 0 = every whole frame in the image.
    static SpriteSheet loadGrid(gfx::VulkanContext& gpu, const std::string& imagePath, glm::ivec2 frameSize,
                                int frameCount = 0, const gfx::TextureDesc& desc = pixelArt());
    // Loads Aseprite's sprite-sheet export (JSON, "Array" or "Hash", with the image next to
    // it). Frame tags become clips: "pingpong" tags ping-pong, "reverse" tags play backwards,
    // and tags with Repeat = 1 play once. Export untrimmed. Aborts with a message on errors.
    static SpriteSheet loadAseprite(gfx::VulkanContext& gpu, const std::string& jsonPath,
                                    const gfx::TextureDesc& desc = pixelArt());

    // The same without loading a texture (tests and tools); attach one with setTexture.
    static SpriteSheet fromGrid(glm::ivec2 imageSize, glm::ivec2 frameSize, int frameCount = 0);
    // Returns an empty sheet and sets `error` if the JSON isn't an Aseprite export.
    static SpriteSheet fromAsepriteJson(std::string_view json, std::string* imageFile = nullptr,
                                        std::string* error = nullptr);

    // Sprite sheets want crisp, separate frames: nearest filtering, no mipmaps (which
    // would blend neighbouring frames), clamped edges. For smooth art use LINEAR, and
    // leave 1-2 pixels of empty space between frames.
    static gfx::TextureDesc pixelArt();

    void setTexture(gfx::Texture texture) { texture_ = std::move(texture); }
    const gfx::Texture& texture() const { return texture_; }
    bool valid() const { return !frames_.empty(); }

    int frameCount() const { return int(frames_.size()); }
    const SheetFrame& frame(int index) const;

    // Adds (or replaces) a clip of `count` consecutive frames starting at `first`.
    AnimationClip& addClip(const std::string& name, int first, int count, float framesPerSecond,
                           AnimationLoop loop = AnimationLoop::Loop);
    // Adds (or replaces) a clip of any frames, in any order, each with its own duration.
    AnimationClip& addClip(AnimationClip clip);
    // nullptr (and a warning, once per name) if there is no such clip.
    const AnimationClip* clip(std::string_view name) const;
    AnimationClip* clip(std::string_view name);
    std::vector<std::string> clipNames() const;

private:
    gfx::Texture texture_;
    std::vector<SheetFrame> frames_;
    std::map<std::string, AnimationClip, std::less<>> clips_;
    mutable std::set<std::string, std::less<>> warned_; // missing clip names already reported
};

// Plays one clip at a time. Cheap to copy; each animated object has its own.
// It points at the SpriteSheet it plays, so keep the sheet alive and in place (a
// member loaded in onInit is ideal) while animators use it.
class Animator {
public:
    // Starts a clip. Asking for the clip that's already playing does nothing (so it is
    // safe to call every frame), unless `restart` is true.
    void play(const SpriteSheet& sheet, std::string_view clip, bool restart = false);
    void stop(); // nothing playing; uv() returns the whole texture

    // Advances time. Call once per step (onFixedUpdate for gameplay-accurate timing,
    // onUpdate for pure decoration). Collects the events whose frames started.
    void update(float dt);

    float speed = 1.0f; // 2 = twice as fast; 0 = paused

    bool playing() const { return clip_ != nullptr; }
    bool isPlaying(std::string_view clip) const;
    const std::string& clipName() const;
    bool finished() const { return finished_; } // a "Once" clip reached its last frame's end
    int frame() const { return index_; }        // index within the clip
    float clipTime() const { return elapsed_; } // seconds since the clip started (looping wraps)

    // The frame to draw. Flip for characters facing left (draw art facing right).
    UvRect uv(bool flipX = false) const;
    glm::vec2 frameSize() const; // pixels; handy for Sprite::size

    // Events fired during the last update(), in order (an event can appear twice
    // after a long frame with a short looping clip).
    const std::vector<std::string>& events() const { return events_; }
    bool eventFired(std::string_view name) const;

private:
    void enterFrame(int index); // makes `index` current and queues its events
    int nextIndex(bool& wrapped) const;

    const SpriteSheet* sheet_ = nullptr;
    const AnimationClip* clip_ = nullptr;
    int index_ = 0;
    int direction_ = 1; // ping-pong: +1 forwards, -1 backwards
    float frameTime_ = 0.0f;
    float elapsed_ = 0.0f;
    bool finished_ = false;
    std::vector<std::string> pending_; // events of the first frame, fired by the next update()
    std::vector<std::string> events_;
};

} // namespace render2d
