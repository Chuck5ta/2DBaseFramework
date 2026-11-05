#include "core/Actions.h"

#include "core/Input.h"
#include "core/Log.h"

#include <SDL3/SDL_mouse.h>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace core {

namespace {

const std::vector<Binding>& noBindings()
{
    static const std::vector<Binding> none;
    return none;
}

// A gamepad axis as a signed value: sticks -1..1 (with Input's dead zone), triggers 0..1.
float axisValue(const Input& input, int axis)
{
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX: return input.leftStick().x;
    case SDL_GAMEPAD_AXIS_LEFTY: return input.leftStick().y;
    case SDL_GAMEPAD_AXIS_RIGHTX: return input.rightStick().x;
    case SDL_GAMEPAD_AXIS_RIGHTY: return input.rightStick().y;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return input.leftTrigger();
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return input.rightTrigger();
    default: return 0.0f;
    }
}

enum class PadStyle { Xbox, PlayStation, Nintendo };

PadStyle padStyle(SDL_Gamepad* pad)
{
    switch (pad ? SDL_GetGamepadType(pad) : SDL_GAMEPAD_TYPE_STANDARD) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return PadStyle::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return PadStyle::Nintendo;
    default:
        return PadStyle::Xbox; // also what the Steam Deck looks like through Steam Input
    }
}

std::string buttonName(SDL_Gamepad* pad, SDL_GamepadButton button)
{
    const PadStyle style = padStyle(pad);
    // Face buttons: the letters/symbols printed on this pad.
    const SDL_GamepadType type = pad ? SDL_GetGamepadType(pad) : SDL_GAMEPAD_TYPE_XBOXONE;
    switch (SDL_GetGamepadButtonLabelForType(type, button)) {
    case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
    case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
    case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
    default: break;
    }
    const bool ps = style == PadStyle::PlayStation, nin = style == PadStyle::Nintendo;
    switch (button) {
    case SDL_GAMEPAD_BUTTON_BACK: return ps ? "Share" : nin ? "-" : "View";
    case SDL_GAMEPAD_BUTTON_START: return ps ? "Options" : nin ? "+" : "Menu";
    case SDL_GAMEPAD_BUTTON_GUIDE: return ps ? "PS" : nin ? "Home" : "Guide";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return ps ? "L3" : "Left Stick Click";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return ps ? "R3" : "Right Stick Click";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ps ? "L1" : nin ? "L" : "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ps ? "R1" : nin ? "R" : "RB";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad Up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad Down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad Left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad Right";
    default: return "Button " + std::to_string(int(button));
    }
}

std::string axisName(SDL_Gamepad* pad, SDL_GamepadAxis axis, int direction)
{
    const PadStyle style = padStyle(pad);
    const bool neg = direction < 0;
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX: return neg ? "Left Stick Left" : "Left Stick Right";
    case SDL_GAMEPAD_AXIS_LEFTY: return neg ? "Left Stick Up" : "Left Stick Down";
    case SDL_GAMEPAD_AXIS_RIGHTX: return neg ? "Right Stick Left" : "Right Stick Right";
    case SDL_GAMEPAD_AXIS_RIGHTY: return neg ? "Right Stick Up" : "Right Stick Down";
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        return style == PadStyle::PlayStation ? "L2" : style == PadStyle::Nintendo ? "ZL" : "LT";
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        return style == PadStyle::PlayStation ? "R2" : style == PadStyle::Nintendo ? "ZR" : "RT";
    default: return "Axis " + std::to_string(int(axis));
    }
}

} // namespace

// --- Binding --------------------------------------------------------------------

std::string Binding::toString() const
{
    switch (kind) {
    case Kind::Key: {
        const char* name = SDL_GetScancodeName(SDL_Scancode(code));
        return (name && *name) ? std::string("key:") + name : "key:#" + std::to_string(code);
    }
    case Kind::MouseButton:
        return "mouse:" + std::to_string(code);
    case Kind::GamepadButton: {
        const char* name = SDL_GetGamepadStringForButton(SDL_GamepadButton(code));
        return std::string("pad:") + (name ? name : "?");
    }
    case Kind::GamepadAxis: {
        const char* name = SDL_GetGamepadStringForAxis(SDL_GamepadAxis(code));
        return std::string("axis:") + (name ? name : "?") + (direction < 0 ? "-" : "+");
    }
    default:
        return "none";
    }
}

std::optional<Binding> Binding::fromString(std::string_view text)
{
    const size_t colon = text.find(':');
    if (colon == std::string_view::npos)
        return std::nullopt;
    const std::string kind(text.substr(0, colon));
    std::string rest(text.substr(colon + 1));
    if (rest.empty())
        return std::nullopt;
    if (kind == "key") {
        if (rest[0] == '#' && rest.size() > 1)
            return key(SDL_Scancode(std::atoi(rest.c_str() + 1)));
        const SDL_Scancode sc = SDL_GetScancodeFromName(rest.c_str());
        if (sc == SDL_SCANCODE_UNKNOWN)
            return std::nullopt;
        return key(sc);
    }
    if (kind == "mouse") {
        const int b = std::atoi(rest.c_str());
        if (b < 1 || b > 7)
            return std::nullopt;
        return mouse(b);
    }
    if (kind == "pad") {
        const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(rest.c_str());
        if (b == SDL_GAMEPAD_BUTTON_INVALID)
            return std::nullopt;
        return button(b);
    }
    if (kind == "axis") {
        int dir = 1;
        if (rest.back() == '+' || rest.back() == '-') {
            dir = rest.back() == '-' ? -1 : 1;
            rest.pop_back();
        }
        const SDL_GamepadAxis a = SDL_GetGamepadAxisFromString(rest.c_str());
        if (a == SDL_GAMEPAD_AXIS_INVALID)
            return std::nullopt;
        return axis(a, dir);
    }
    return std::nullopt;
}

// --- Defining actions ---------------------------------------------------------------

void Actions::define(const std::string& action, std::initializer_list<Binding> defaults)
{
    define(action, std::vector<Binding>(defaults));
}

void Actions::define(const std::string& action, std::vector<Binding> defaults)
{
    if (!defined(action))
        order_.push_back(action);
    Action& a = actions_[action];
    a.defaults = defaults;
    a.bindings = std::move(defaults);
}

bool Actions::defined(std::string_view action) const
{
    return actions_.find(action) != actions_.end();
}

const Actions::Action* Actions::find(std::string_view name) const
{
    const auto it = actions_.find(name);
    if (it != actions_.end())
        return &it->second;
    if (warned_.find(name) == warned_.end()) {
        warned_.emplace(name);
        LOG_WARN("Input: no action called '%.*s' (define it first)", int(name.size()), name.data());
    }
    return nullptr;
}

Actions::Action* Actions::find(std::string_view name)
{
    return const_cast<Action*>(static_cast<const Actions*>(this)->find(name));
}

// --- Reading ------------------------------------------------------------------------

bool Actions::down(std::string_view action) const
{
    const Action* a = find(action);
    return a && a->down;
}

bool Actions::pressed(std::string_view action) const
{
    const Action* a = find(action);
    return a && a->pressed;
}

bool Actions::released(std::string_view action) const
{
    const Action* a = find(action);
    return a && a->released;
}

float Actions::value(std::string_view action) const
{
    const Action* a = find(action);
    return a ? a->value : 0.0f;
}

glm::vec2 Actions::vector(std::string_view negativeX, std::string_view positiveX, std::string_view negativeY,
                          std::string_view positiveY) const
{
    glm::vec2 v(value(positiveX) - value(negativeX), value(positiveY) - value(negativeY));
    const float length = glm::length(v);
    return length > 1.0f ? v / length : v; // diagonals on keys/d-pad aren't faster
}

bool Actions::consume(std::string_view action, float bufferSeconds)
{
    Action* a = find(action);
    if (!a || a->consumed || now_ - a->lastPress > double(bufferSeconds))
        return false;
    a->consumed = true;
    return true;
}

// --- Rebinding ----------------------------------------------------------------------

const std::vector<Binding>& Actions::bindings(std::string_view action) const
{
    const Action* a = find(action);
    return a ? a->bindings : noBindings();
}

const std::vector<Binding>& Actions::defaultBindings(std::string_view action) const
{
    const Action* a = find(action);
    return a ? a->defaults : noBindings();
}

void Actions::setBindings(std::string_view action, std::vector<Binding> list)
{
    if (Action* a = find(action))
        a->bindings = std::move(list);
}

void Actions::addBinding(std::string_view action, const Binding& binding)
{
    Action* a = find(action);
    if (a && binding.valid() && std::find(a->bindings.begin(), a->bindings.end(), binding) == a->bindings.end())
        a->bindings.push_back(binding);
}

void Actions::removeBinding(std::string_view action, const Binding& binding)
{
    if (Action* a = find(action))
        a->bindings.erase(std::remove(a->bindings.begin(), a->bindings.end(), binding), a->bindings.end());
}

void Actions::resetToDefaults(std::string_view action)
{
    if (Action* a = find(action))
        a->bindings = a->defaults;
}

void Actions::resetAllToDefaults()
{
    for (auto& [name, a] : actions_)
        a.bindings = a.defaults;
}

std::vector<std::string> Actions::actionsUsing(const Binding& binding) const
{
    std::vector<std::string> out;
    for (const auto& [name, a] : actions_)
        if (std::find(a.bindings.begin(), a.bindings.end(), binding) != a.bindings.end())
            out.push_back(name);
    return out;
}

std::string Actions::saveBindings() const
{
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    for (const std::string& name : order_) { // in definition order, easy to read
        nlohmann::ordered_json list = nlohmann::ordered_json::array();
        for (const Binding& b : actions_.find(name)->second.bindings)
            list.push_back(b.toString());
        j[name] = std::move(list);
    }
    return j.dump(2);
}

bool Actions::loadBindings(std::string_view text)
{
    const nlohmann::json j = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return false;
    for (const auto& [name, list] : j.items()) {
        const auto it = actions_.find(name);
        if (it == actions_.end() || !list.is_array())
            continue; // an action this version of the game doesn't have
        std::vector<Binding> loaded;
        for (const auto& entry : list)
            if (entry.is_string())
                if (const auto b = Binding::fromString(entry.get<std::string>()))
                    loaded.push_back(*b);
        it->second.bindings = std::move(loaded);
    }
    return true;
}

// --- Showing controls -----------------------------------------------------------------

std::string Actions::displayName(const Binding& b) const
{
    SDL_Gamepad* pad = input_ ? input_->gamepad() : nullptr;
    switch (b.kind) {
    case Binding::Kind::Key: {
        const SDL_Scancode sc = SDL_Scancode(b.code);
        const char* name = SDL_GetScancodeName(sc);
        std::string text = (name && *name) ? name : "Key " + std::to_string(b.code);
        if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN)
            text += " Arrow";
        return text;
    }
    case Binding::Kind::MouseButton:
        switch (b.code) {
        case SDL_BUTTON_LEFT: return "Left Mouse";
        case SDL_BUTTON_RIGHT: return "Right Mouse";
        case SDL_BUTTON_MIDDLE: return "Middle Mouse";
        default: return "Mouse " + std::to_string(b.code);
        }
    case Binding::Kind::GamepadButton:
        return buttonName(pad, SDL_GamepadButton(b.code));
    case Binding::Kind::GamepadAxis:
        return axisName(pad, SDL_GamepadAxis(b.code), b.direction);
    default:
        return "-";
    }
}

std::string Actions::prompt(std::string_view action) const
{
    const Action* a = find(action);
    if (!a || a->bindings.empty())
        return "(unbound)";
    const bool wantPad = lastDevice_ == InputDevice::Gamepad;
    for (const Binding& b : a->bindings)
        if (b.isGamepad() == wantPad)
            return displayName(b);
    return displayName(a->bindings.front());
}

// --- Per-frame update -----------------------------------------------------------------

float Actions::bindingValue(const Binding& b) const
{
    const Input& in = *input_;
    switch (b.kind) {
    case Binding::Kind::Key: return in.keyDown(SDL_Scancode(b.code)) ? 1.0f : 0.0f;
    case Binding::Kind::MouseButton: return in.mouseDown(b.code) ? 1.0f : 0.0f;
    case Binding::Kind::GamepadButton: return in.gamepadDown(SDL_GamepadButton(b.code)) ? 1.0f : 0.0f;
    case Binding::Kind::GamepadAxis: return std::clamp(float(b.direction) * axisValue(in, b.code), 0.0f, 1.0f);
    default: return 0.0f;
    }
}

bool Actions::bindingPressed(const Binding& b) const
{
    switch (b.kind) {
    case Binding::Kind::Key: return input_->keyPressed(SDL_Scancode(b.code));
    case Binding::Kind::MouseButton: return input_->mousePressed(b.code);
    case Binding::Kind::GamepadButton: return input_->gamepadPressed(SDL_GamepadButton(b.code));
    default: return false; // axes: crossing the threshold, handled in update()
    }
}

bool Actions::bindingReleased(const Binding& b) const
{
    switch (b.kind) {
    case Binding::Kind::Key: return input_->keyReleased(SDL_Scancode(b.code));
    case Binding::Kind::MouseButton: return input_->mouseReleased(b.code);
    case Binding::Kind::GamepadButton: return input_->gamepadReleased(SDL_GamepadButton(b.code));
    default: return false;
    }
}

void Actions::update(const Input& input, double timeSeconds)
{
    input_ = &input;
    now_ = timeSeconds;
    captured_.reset();

    // Which device is in use, and the first thing pressed this frame (for rebinding).
    for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc)
        if (input.keyPressed(SDL_Scancode(sc))) {
            lastDevice_ = InputDevice::KeyboardMouse;
            if (!captured_)
                captured_ = Binding::key(SDL_Scancode(sc));
        }
    for (int b = SDL_BUTTON_LEFT; b <= SDL_BUTTON_X2; ++b)
        if (input.mousePressed(b)) {
            lastDevice_ = InputDevice::KeyboardMouse;
            if (!captured_)
                captured_ = Binding::mouse(b);
        }
    if (glm::length(input.mouseDelta()) > 4.0f && !input.relativeMouseMode())
        lastDevice_ = InputDevice::KeyboardMouse;
    if (input.hasGamepad()) {
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
            if (input.gamepadPressed(SDL_GamepadButton(b))) {
                lastDevice_ = InputDevice::Gamepad;
                if (!captured_)
                    captured_ = Binding::button(SDL_GamepadButton(b));
            }
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
            const float v = axisValue(input, a);
            if (std::abs(v) >= pressThreshold && std::abs(prevAxes_[a]) < pressThreshold) {
                lastDevice_ = InputDevice::Gamepad;
                if (!captured_)
                    captured_ = Binding::axis(SDL_GamepadAxis(a), v < 0.0f ? -1 : 1);
            }
            prevAxes_[a] = v;
        }
    }

    // Each action: the strongest of its bindings.
    for (auto& [name, a] : actions_) {
        a.wasDown = a.down;
        float value = 0.0f;
        bool tapped = false, letGo = false;
        for (const Binding& b : a.bindings) {
            value = std::max(value, bindingValue(b));
            tapped = tapped || bindingPressed(b);
            letGo = letGo || bindingReleased(b);
        }
        a.value = value;
        a.down = value >= pressThreshold;
        // A press and release within one frame still counts as a press (and a release).
        a.pressed = !a.wasDown && (a.down || tapped);
        a.released = !a.down && (a.wasDown || (letGo && a.pressed));
        if (a.pressed) {
            a.lastPress = timeSeconds;
            a.consumed = false;
        }
    }
}

} // namespace core
