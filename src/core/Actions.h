#pragma once

// Input actions: the game asks about "jump", not about Space or the A button.
//
//   // Once, in onInit (the defaults; players can rebind them):
//   auto& actions = app.actions();
//   actions.define("jump", {Binding::key(SDL_SCANCODE_SPACE), Binding::button(SDL_GAMEPAD_BUTTON_SOUTH)});
//   actions.define("left", {Binding::key(SDL_SCANCODE_A), Binding::key(SDL_SCANCODE_LEFT),
//                           Binding::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT),
//                           Binding::axis(SDL_GAMEPAD_AXIS_LEFTX, -1)});
//
//   // Every frame (onUpdate):
//   if (actions.pressed("jump")) ...
//   glm::vec2 move = actions.vector("left", "right", "up", "down"); // analog with a stick
//
//   // In onFixedUpdate, buffered (a press is kept for a moment until used):
//   if (actions.consume("punch")) ...
//
//   // Showing the controls: "Press Space" or "Press A", for whatever was used last.
//   hud.drawText(font, "Jump: " + actions.prompt("jump"), ...);

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>
#include <glm/vec2.hpp>

#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace core {

class Input;

// One physical input that can trigger an action.
struct Binding {
    enum class Kind : uint8_t { None, Key, MouseButton, GamepadButton, GamepadAxis };
    Kind kind = Kind::None;
    int code = 0;      // SDL_Scancode, mouse button (SDL_BUTTON_LEFT...), SDL_GamepadButton or SDL_GamepadAxis
    int direction = 1; // axes: +1 or -1 (sticks: +X = right, +Y = down; triggers: +1)

    static Binding key(SDL_Scancode scancode) { return {Kind::Key, int(scancode), 1}; }
    static Binding mouse(int button) { return {Kind::MouseButton, button, 1}; }
    static Binding button(SDL_GamepadButton button) { return {Kind::GamepadButton, int(button), 1}; }
    static Binding axis(SDL_GamepadAxis axis, int direction = 1) { return {Kind::GamepadAxis, int(axis), direction < 0 ? -1 : 1}; }

    bool valid() const { return kind != Kind::None; }
    bool isGamepad() const { return kind == Kind::GamepadButton || kind == Kind::GamepadAxis; }

    // Stable text for saving: "key:Space", "mouse:1", "pad:a", "axis:leftx-".
    std::string toString() const;
    static std::optional<Binding> fromString(std::string_view text);

    bool operator==(const Binding&) const = default;
};

enum class InputDevice { KeyboardMouse, Gamepad };

class Actions {
public:
    // Creates (or redefines) an action with its default bindings, which are also its
    // current bindings until changed.
    void define(const std::string& action, std::initializer_list<Binding> defaults);
    void define(const std::string& action, std::vector<Binding> defaults);
    bool defined(std::string_view action) const;
    const std::vector<std::string>& names() const { return order_; } // in the order they were defined

    // --- Reading actions (unknown names warn once and read as idle) -----------------
    bool down(std::string_view action) const;     // held now
    bool pressed(std::string_view action) const;  // started this frame (short taps are never missed)
    bool released(std::string_view action) const; // ended this frame
    float value(std::string_view action) const;   // 0..1: analog for sticks and triggers
    // A 2D direction from four actions, e.g. ("left", "right", "up", "down"). Analog with
    // a stick, length at most 1. +Y is "down" (screen coordinates); negate for 3D.
    glm::vec2 vector(std::string_view negativeX, std::string_view positiveX, std::string_view negativeY,
                     std::string_view positiveY) const;

    // Buffered press for fixed-step gameplay: true if the action was pressed within the
    // last `bufferSeconds` and not consumed yet. Then it's used up. A jump pressed just
    // before landing still happens; a punch press between fixed steps is never lost.
    bool consume(std::string_view action, float bufferSeconds = 0.15f);

    // --- Rebinding -----------------------------------------------------------------
    const std::vector<Binding>& bindings(std::string_view action) const;
    const std::vector<Binding>& defaultBindings(std::string_view action) const;
    void setBindings(std::string_view action, std::vector<Binding> bindings);
    void addBinding(std::string_view action, const Binding& binding);    // ignored if already there
    void removeBinding(std::string_view action, const Binding& binding);
    void resetToDefaults(std::string_view action);
    void resetAllToDefaults();
    std::vector<std::string> actionsUsing(const Binding& binding) const; // for conflict warnings

    // The first input pressed this frame (any key, mouse button, gamepad button, or a
    // stick or trigger pushed past halfway), for "press a key for Jump" screens. Use it
    // while the player is choosing; empty if nothing was pressed.
    std::optional<Binding> capture() const { return captured_; }

    // Bindings as JSON text, for a settings file: {"jump": ["key:Space", "pad:a"], ...}.
    // load returns false for text it can't read; unknown actions and inputs are skipped.
    std::string saveBindings() const;
    bool loadBindings(std::string_view json);

    // --- Showing controls ------------------------------------------------------------
    // The device the player used most recently (switches on any key, click, button or
    // stick movement).
    InputDevice lastDevice() const { return lastDevice_; }
    // The first binding of `action` for that device, as a name: "Space", "A", "Left Stick".
    // Gamepad names follow the connected pad's labels (A/B/X/Y or Cross/Circle/...).
    std::string prompt(std::string_view action) const;
    std::string displayName(const Binding& binding) const;

    float pressThreshold = 0.5f; // how far a stick/trigger must go to count as "down"

    // --- Called by Application once per frame, after input events ------------------
    void update(const Input& input, double timeSeconds);

private:
    struct Action {
        std::vector<Binding> defaults;
        std::vector<Binding> bindings;
        bool down = false, wasDown = false;
        bool pressed = false, released = false;
        float value = 0.0f;
        double lastPress = -1.0e9; // time of the latest press, for consume()
        bool consumed = true;
    };
    const Action* find(std::string_view name) const;
    Action* find(std::string_view name);
    float bindingValue(const Binding& b) const;
    bool bindingPressed(const Binding& b) const;
    bool bindingReleased(const Binding& b) const;

    std::map<std::string, Action, std::less<>> actions_;
    std::vector<std::string> order_; // names in definition order
    mutable std::set<std::string, std::less<>> warned_;
    const Input* input_ = nullptr;
    double now_ = 0.0;
    InputDevice lastDevice_ = InputDevice::KeyboardMouse;
    std::optional<Binding> captured_;
    float prevAxes_[SDL_GAMEPAD_AXIS_COUNT] = {};
};

} // namespace core
