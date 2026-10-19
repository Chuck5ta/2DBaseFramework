#include "core/Input.h"

#include "core/Log.h"

#include <glm/geometric.hpp>

#include <algorithm>

namespace core {

Input::~Input()
{
    closeGamepad();
}

void Input::beginFrame()
{
    for (auto& k : keys_)         { k.pressed = false; k.released = false; }
    for (auto& b : mouseButtons_) { b.pressed = false; b.released = false; }
    for (auto& b : padButtons_)   { b.pressed = false; b.released = false; }
    mouseDelta_ = glm::vec2(0.0f);
    wheel_ = 0.0f;
}

void Input::handleEvent(const SDL_Event& e)
{
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
        if (!e.key.repeat && e.key.scancode < SDL_SCANCODE_COUNT)
            press(keys_[e.key.scancode]);
        break;
    case SDL_EVENT_KEY_UP:
        if (e.key.scancode < SDL_SCANCODE_COUNT)
            release(keys_[e.key.scancode]);
        break;

    case SDL_EVENT_MOUSE_MOTION:
        mousePos_ = glm::vec2(e.motion.x, e.motion.y) * pixelDensity_;
        mouseDelta_ += glm::vec2(e.motion.xrel, e.motion.yrel) * pixelDensity_;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button < mouseButtons_.size())
            press(mouseButtons_[e.button.button]);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button < mouseButtons_.size())
            release(mouseButtons_[e.button.button]);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        wheel_ += e.wheel.y;
        break;

    case SDL_EVENT_GAMEPAD_ADDED:
        if (!gamepad_)
            openFirstGamepad();
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (gamepad_ && e.gdevice.which == gamepadId_) {
            closeGamepad();
            openFirstGamepad(); // fall back to any other connected pad
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (e.gbutton.which == gamepadId_ && e.gbutton.button < padButtons_.size())
            press(padButtons_[e.gbutton.button]);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (e.gbutton.which == gamepadId_ && e.gbutton.button < padButtons_.size())
            release(padButtons_[e.gbutton.button]);
        break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // Avoid "stuck" keys when alt-tabbing away.
        for (auto& k : keys_)         release(k);
        for (auto& b : mouseButtons_) release(b);
        break;
    default:
        break;
    }
}

bool Input::mouseDown(int button) const
{
    return button >= 0 && button < int(mouseButtons_.size()) && mouseButtons_[button].down;
}

bool Input::mousePressed(int button) const
{
    return button >= 0 && button < int(mouseButtons_.size()) && mouseButtons_[button].pressed;
}

bool Input::mouseReleased(int button) const
{
    return button >= 0 && button < int(mouseButtons_.size()) && mouseButtons_[button].released;
}

void Input::setRelativeMouseMode(SDL_Window* window, bool enabled)
{
    if (SDL_SetWindowRelativeMouseMode(window, enabled))
        relativeMouse_ = enabled;
}

const char* Input::gamepadName() const
{
    return gamepad_ ? SDL_GetGamepadName(gamepad_) : "";
}

bool Input::gamepadDown(SDL_GamepadButton b) const
{
    return b >= 0 && b < SDL_GAMEPAD_BUTTON_COUNT && padButtons_[b].down;
}

bool Input::gamepadPressed(SDL_GamepadButton b) const
{
    return b >= 0 && b < SDL_GAMEPAD_BUTTON_COUNT && padButtons_[b].pressed;
}

bool Input::gamepadReleased(SDL_GamepadButton b) const
{
    return b >= 0 && b < SDL_GAMEPAD_BUTTON_COUNT && padButtons_[b].released;
}

float Input::axis(SDL_GamepadAxis a) const
{
    if (!gamepad_)
        return 0.0f;
    const Sint16 raw = SDL_GetGamepadAxis(gamepad_, a);
    return std::clamp(float(raw) / 32767.0f, -1.0f, 1.0f);
}

glm::vec2 Input::stick(SDL_GamepadAxis x, SDL_GamepadAxis y) const
{
    glm::vec2 v(axis(x), axis(y));
    const float len = glm::length(v);
    if (len <= stickDeadZone)
        return glm::vec2(0.0f);
    // Rescale so output ramps smoothly from 0 at the dead zone edge to 1.
    const float scaled = std::min((len - stickDeadZone) / (1.0f - stickDeadZone), 1.0f);
    return v / len * scaled;
}

glm::vec2 Input::leftStick() const  { return stick(SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY); }
glm::vec2 Input::rightStick() const { return stick(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY); }
float Input::leftTrigger() const    { return std::max(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.0f); }
float Input::rightTrigger() const   { return std::max(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0.0f); }

void Input::rumble(float low, float high, int milliseconds) const
{
    if (!gamepad_)
        return;
    auto toU16 = [](float v) { return Uint16(std::clamp(v, 0.0f, 1.0f) * 65535.0f); };
    SDL_RumbleGamepad(gamepad_, toU16(low), toU16(high), Uint32(std::max(milliseconds, 0)));
}

void Input::openFirstGamepad()
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int i = 0; i < count && !gamepad_; ++i) {
            gamepad_ = SDL_OpenGamepad(ids[i]);
            if (gamepad_) {
                gamepadId_ = ids[i];
                LOG_INFO("Gamepad connected: %s", SDL_GetGamepadName(gamepad_));
            }
        }
        SDL_free(ids);
    }
}

void Input::closeGamepad()
{
    if (gamepad_) {
        LOG_INFO("Gamepad disconnected: %s", SDL_GetGamepadName(gamepad_));
        SDL_CloseGamepad(gamepad_);
    }
    gamepad_ = nullptr;
    gamepadId_ = 0;
    for (auto& b : padButtons_)
        b = ButtonState{};
}

} // namespace core
