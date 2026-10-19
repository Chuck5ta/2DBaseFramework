#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

namespace render2d {

// Orthographic 2D camera. World units are "pixels at zoom 1", +X right,
// +Y down (like most 2D tools and tile editors).
//
// referenceHeight keeps the visible world area consistent across resolutions:
// with referenceHeight = 360, the camera always shows 360 world units
// vertically whether the window is 720p, Steam Deck 800p or 4K. Set it to 0
// to map 1 world unit to 1 screen pixel instead.
class Camera2D {
public:
    glm::vec2 position{0.0f};   // world point at the centre of the screen
    float zoom = 1.0f;          // >1 zooms in
    float rotation = 0.0f;      // radians
    float referenceHeight = 0.0f;

    void setViewport(float widthPixels, float heightPixels) { viewport_ = {widthPixels, heightPixels}; }
    glm::vec2 viewport() const { return viewport_; }

    // Screen pixels per world unit, including referenceHeight scaling.
    float scale() const;

    glm::mat4 view() const;
    glm::mat4 projection() const;
    glm::mat4 viewProjection() const { return projection() * view(); }

    glm::vec2 screenToWorld(glm::vec2 screenPixels) const;
    glm::vec2 worldToScreen(glm::vec2 world) const;

    // Axis-aligned world-space bounds of what is visible (for culling).
    void visibleBounds(glm::vec2& min, glm::vec2& max) const;

private:
    glm::vec2 viewport_{1280.0f, 720.0f};
};

} // namespace render2d
