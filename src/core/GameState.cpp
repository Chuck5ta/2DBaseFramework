#include "core/GameState.h"

#include "core/Application.h"
#include "core/Log.h"

namespace core {

void StateStack::push(std::unique_ptr<GameState> state)
{
    if (state)
        pending_.push_back({Op::Push, std::move(state)});
}

void StateStack::pop()
{
    pending_.push_back({Op::Pop, nullptr});
}

void StateStack::replace(std::unique_ptr<GameState> state)
{
    pop();
    push(std::move(state));
}

void StateStack::clear()
{
    pending_.push_back({Op::Clear, nullptr});
}

size_t StateStack::firstActive() const
{
    for (size_t i = states_.size(); i > 0; --i)
        if (states_[i - 1]->pausesBelow)
            return i;
    return 0;
}

size_t StateStack::firstVisible() const
{
    for (size_t i = states_.size(); i > 0; --i)
        if (states_[i - 1]->hidesBelow)
            return i;
    return 0;
}

void StateStack::applyChanges(Application& app, Game& game)
{
    if (pending_.empty())
        return;

    // onEnter/onExit may queue more changes (a splash screen that replaces itself at
    // once, say); keep going until none are left, with a limit in case two states
    // keep pushing each other.
    for (int round = 0; !pending_.empty(); ++round) {
        if (round == 16) {
            LOG_WARN("Game states: changes keep queuing more changes; stopping for this frame");
            break;
        }
        std::vector<Change> changes = std::move(pending_);
        pending_.clear();
        for (Change& c : changes) {
            switch (c.op) {
            case Op::Push:
                states_.push_back(std::move(c.state));
                states_.back()->time_ = 0.0f;
                states_.back()->active_ = true;
                states_.back()->onEnter(app);
                break;
            case Op::Pop:
                if (!states_.empty()) {
                    states_.back()->onExit(app);
                    states_.pop_back();
                }
                break;
            case Op::Clear:
                while (!states_.empty()) {
                    states_.back()->onExit(app);
                    states_.pop_back();
                }
                break;
            }
        }
    }

    // Tell every layer whose paused state changed: the Game first, then upwards.
    const size_t active = firstActive();
    const bool gameNowActive = active == 0;
    if (gameNowActive != gameActive_) {
        gameActive_ = gameNowActive;
        if (gameActive_)
            game.onResume(app);
        else
            game.onPause(app);
    }
    for (size_t i = 0; i < states_.size(); ++i) {
        GameState& s = *states_[i];
        const bool nowActive = i + 1 >= active;
        if (nowActive != s.active_) {
            s.active_ = nowActive;
            if (nowActive)
                s.onResume(app);
            else
                s.onPause(app);
        }
    }

    // A press that closed a menu must not also act in the game underneath (a buffered
    // jump on the same button, say).
    app.actions().clearPresses();
}

void StateStack::fixedUpdate(Application& app, Game& game, float dt)
{
    const size_t active = firstActive();
    if (active == 0)
        game.onFixedUpdate(app, dt);
    for (size_t i = active > 0 ? active - 1 : 0; i < states_.size(); ++i)
        states_[i]->onFixedUpdate(app, dt);
}

void StateStack::update(Application& app, Game& game, float dt)
{
    const size_t active = firstActive();
    if (active == 0)
        game.onUpdate(app, dt);
    for (size_t i = active > 0 ? active - 1 : 0; i < states_.size(); ++i) {
        states_[i]->time_ += dt;
        states_[i]->onUpdate(app, dt);
    }
}

void StateStack::preRender(Application& app, Game& game, gfx::FrameContext& frame)
{
    const size_t visible = firstVisible();
    if (visible == 0)
        game.onPreRender(app, frame);
    for (size_t i = visible > 0 ? visible - 1 : 0; i < states_.size(); ++i)
        states_[i]->onPreRender(app, frame);
}

void StateStack::render(Application& app, Game& game, gfx::FrameContext& frame)
{
    const size_t visible = firstVisible();
    if (visible == 0)
        game.onRender(app, frame);
    for (size_t i = visible > 0 ? visible - 1 : 0; i < states_.size(); ++i)
        states_[i]->onRender(app, frame);
}

void StateStack::resize(Application& app, Game& game, uint32_t width, uint32_t height)
{
    game.onResize(app, width, height);
    for (auto& s : states_)
        s->onResize(app, width, height);
}

void StateStack::debugUI(Application& app)
{
    for (auto& s : states_)
        s->onDebugUI(app);
}

void StateStack::shutdown(Application& app)
{
    pending_.clear(); // queued states were never entered; they are just destroyed
    while (!states_.empty()) {
        states_.back()->onExit(app);
        states_.pop_back();
    }
}

} // namespace core
