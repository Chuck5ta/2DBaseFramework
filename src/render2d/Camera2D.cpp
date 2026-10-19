#include "render2d/Camera2D.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>

namespace render2d {

float Camera2D::scale() const
{
    const float base = referenceHeight > 0.0f ? viewport_.y / referenceHeight : 1.0f;
    return base * zoom;
}

glm::mat4 Camera2D::view() const
{
    // world -> screen pixels: move camera to origin, rotate, scale, then
    // centre on the viewport.
    glm::mat4 m(1.0f);
    m = glm::translate(m, glm::vec3(viewport_ * 0.5f, 0.0f));
    m = glm::scale(m, glm::vec3(scale(), scale(), 1.0f));
    m = glm::rotate(m, -rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::translate(m, glm::vec3(-position, 0.0f));
    return m;
}

glm::mat4 Camera2D::projection() const
{
    // Pixels -> clip space. Vulkan's clip space has +Y pointing down, so
    // bottom = 0 / top = height gives a y-down screen without any flipping.
    return glm::ortho(0.0f, viewport_.x, 0.0f, viewport_.y, -1.0f, 1.0f);
}

glm::vec2 Camera2D::screenToWorld(glm::vec2 screen) const
{
    const glm::vec4 w = glm::inverse(view()) * glm::vec4(screen, 0.0f, 1.0f);
    return {w.x, w.y};
}

glm::vec2 Camera2D::worldToScreen(glm::vec2 world) const
{
    const glm::vec4 s = view() * glm::vec4(world, 0.0f, 1.0f);
    return {s.x, s.y};
}

void Camera2D::visibleBounds(glm::vec2& outMin, glm::vec2& outMax) const
{
    const glm::vec2 corners[4] = {
        screenToWorld({0.0f, 0.0f}),
        screenToWorld({viewport_.x, 0.0f}),
        screenToWorld({0.0f, viewport_.y}),
        screenToWorld({viewport_.x, viewport_.y}),
    };
    outMin = outMax = corners[0];
    for (const glm::vec2& c : corners) {
        outMin = glm::min(outMin, c);
        outMax = glm::max(outMax, c);
    }
}

} // namespace render2d
