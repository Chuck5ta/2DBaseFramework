#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>
#include <glm/vec2.hpp>

#include <array>

namespace core {

// Keyboard, mouse and gamepad state for the current frame.
//
//   down()     - held this frame
//   pressed()  - went down since last frame (never missed, even for taps
//                shorter than a frame)
//   released() - went up since last frame
//
// On Steam Deck the built-in controls are exposed by Steam Input as a
// standard gamepad, so the gamepad API below covers them. The first
// connected gamepad is used; hot-plugging is handled.
class Input {
public:
    Input() = default;
    ~Input();

    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // Called by Application once per frame, before polling events.
    void beginFrame();
    void handleEvent(const SDL_Event& event);
    void setPixelDensity(float density) { pixelDensity_ = density; }

    // --- Keyboard (use SDL_SCANCODE_* so layouts like AZERTY still work) ---
    bool keyDown(SDL_Scancode key) const     { return keys_[key].down; }
    bool keyPressed(SDL_Scancode key) const  { return keys_[key].pressed; }
    bool keyReleased(SDL_Scancode key) const { return keys_[key].released; }

    // --- Mouse (positions in framebuffer pixels) ---
    // button: SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT, ...
    bool mouseDown(int button) const;
    bool mousePressed(int button) const;
    bool mouseReleased(int button) const;
    glm::vec2 mousePosition() const { return mousePos_; }
    glm::vec2 mouseDelta() const    { return mouseDelta_; }
    float mouseWheel() const        { return wheel_; }

    // Hides the cursor and reports unbounded relative motion (FPS cameras).
    void setRelativeMouseMode(SDL_Window* window, bool enabled);
    bool relativeMouseMode() const { return relativeMouse_; }

    // --- Gamepad ---
    bool hasGamepad() const { return gamepad_ != nullptr; }
    const char* gamepadName() const;
    bool gamepadDown(SDL_GamepadButton button) const;
    bool gamepadPressed(SDL_GamepadButton button) const;
    bool gamepadReleased(SDL_GamepadButton button) const;
    // Sticks are in [-1, 1] (y points down), triggers in [0, 1].
    // A radial dead zone is applied to sticks.
    glm::vec2 leftStick() const;
    glm::vec2 rightStick() const;
    float leftTrigger() const;
    float rightTrigger() const;
    void rumble(float low, float high, int milliseconds) const;

    float stickDeadZone = 0.15f;

private:
    struct ButtonState {
        bool down = false;
        bool pressed = false;
        bool released = false;
    };

    static void press(ButtonState& b)   { if (!b.down) { b.down = true; b.pressed = true; } }
    static void release(ButtonState& b) { if (b.down) { b.down = false; b.released = true; } }

    void openFirstGamepad();
    void closeGamepad();
    glm::vec2 stick(SDL_GamepadAxis x, SDL_GamepadAxis y) const;
    float axis(SDL_GamepadAxis a) const;

    std::array<ButtonState, SDL_SCANCODE_COUNT> keys_{};
    std::array<ButtonState, 8> mouseButtons_{};
    std::array<ButtonState, SDL_GAMEPAD_BUTTON_COUNT> padButtons_{};

    glm::vec2 mousePos_{0.0f};
    glm::vec2 mouseDelta_{0.0f};
    float wheel_ = 0.0f;
    float pixelDensity_ = 1.0f;
    bool relativeMouse_ = false;

    SDL_Gamepad* gamepad_ = nullptr;
    SDL_JoystickID gamepadId_ = 0;
};

} // namespace core
