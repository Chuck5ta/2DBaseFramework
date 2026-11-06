#pragma once

// Game states: screens and modes such as a title screen, a pause menu or a game-over
// screen, kept on a stack on top of your Game.
//
//   class PauseState : public core::GameState {
//   public:
//       PauseState() { name = "Pause"; }      // pausesBelow = true: the game stops
//       void onUpdate(core::Application& app, float) override {
//           if (app.actions().pressed("pause"))
//               app.states().pop();           // back to the game
//       }
//       void onRender(core::Application& app, gfx::FrameContext& frame) override { ... }
//   };
//
//   // In the game's onUpdate:
//   if (app.actions().pressed("pause"))
//       app.states().push<PauseState>();
//
// The Game is always the bottom layer; states are drawn and updated above it, bottom
// to top. A state can pause the layers below it (they get no onFixedUpdate/onUpdate)
// and can hide them (they aren't drawn). push/pop/replace/clear take effect at the
// start of the next frame, so a state can safely remove itself from its own hooks.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gfx { struct FrameContext; }

namespace core {

class Application;
class Game;

class GameState {
public:
    virtual ~GameState() = default;

    // --- What this state does to the layers below it (states and the Game) ---------
    // Set these in the constructor.
    bool pausesBelow = true; // below get no onFixedUpdate / onUpdate (a menu over a frozen game)
    bool hidesBelow = false; // below aren't drawn (a full-screen menu or loading screen)
    std::string name = "State"; // shown in the debug UI's Engine panel

    // --- Lifecycle ---------------------------------------------------------------------
    virtual void onEnter(Application&) {}  // added to the stack
    virtual void onExit(Application&) {}   // removed (popped, replaced or cleared); destroyed next
    virtual void onPause(Application&) {}  // a state that pauses this one was pushed above it
    virtual void onResume(Application&) {} // ...and is gone again

    // --- The same hooks as core::Game, called while this state is active/visible -------
    virtual void onFixedUpdate(Application&, float /*dt*/) {}
    virtual void onUpdate(Application&, float /*dt*/) {}
    virtual void onPreRender(Application&, gfx::FrameContext&) {}
    virtual void onRender(Application&, gfx::FrameContext&) {}
    virtual void onResize(Application&, uint32_t /*width*/, uint32_t /*height*/) {}
    virtual void onDebugUI(Application&) {} // every state, paused or not, while the debug UI shows

    // Seconds since onEnter, counting only the time this state was active (not paused).
    float timeInState() const { return time_; }

private:
    friend class StateStack;
    float time_ = 0.0f;
    bool active_ = true;
};

class StateStack {
public:
    // Changes are queued and applied in order at the start of the next frame.
    void push(std::unique_ptr<GameState> state);
    template <class T, class... Args>
    void push(Args&&... args) { push(std::make_unique<T>(std::forward<Args>(args)...)); }
    void pop();                                     // removes the top state (nothing if empty)
    void replace(std::unique_ptr<GameState> state); // pop, then push: switch screens
    template <class T, class... Args>
    void replace(Args&&... args) { replace(std::make_unique<T>(std::forward<Args>(args)...)); }
    void clear();                                   // removes every state: back to just the Game

    // The stack as it is now (queued changes not applied yet).
    bool empty() const { return states_.empty(); }
    size_t size() const { return states_.size(); }
    GameState* top() const { return states_.empty() ? nullptr : states_.back().get(); }
    GameState* at(size_t index) const { return states_[index].get(); } // 0 = bottom
    bool changesPending() const { return !pending_.empty(); }
    // The first state of type T, from the top (nullptr if none).
    template <class T>
    T* find() const
    {
        for (auto it = states_.rbegin(); it != states_.rend(); ++it)
            if (T* s = dynamic_cast<T*>(it->get()))
                return s;
        return nullptr;
    }
    bool isActive(const GameState& state) const { return state.active_; } // updating (not paused)
    bool gamePaused() const { return !gameActive_; } // a state pauses the Game

    // --- Called by Application ---------------------------------------------------------
    // Applies queued changes and sends onEnter/onExit/onPause/onResume (to the Game too).
    void applyChanges(Application& app, Game& game);
    void fixedUpdate(Application& app, Game& game, float dt);
    void update(Application& app, Game& game, float dt);
    void preRender(Application& app, Game& game, gfx::FrameContext& frame);
    void render(Application& app, Game& game, gfx::FrameContext& frame);
    void resize(Application& app, Game& game, uint32_t width, uint32_t height);
    void debugUI(Application& app);
    void shutdown(Application& app); // onExit for every state, top first; then destroys them

private:
    enum class Op { Push, Pop, Clear };
    struct Change {
        Op op;
        std::unique_ptr<GameState> state;
    };
    size_t firstActive() const;  // lowest layer that updates: 0 = the Game, i = states_[i-1]
    size_t firstVisible() const; // lowest layer that is drawn, same numbering

    std::vector<std::unique_ptr<GameState>> states_;
    std::vector<Change> pending_;
    bool gameActive_ = true;
};

} // namespace core
