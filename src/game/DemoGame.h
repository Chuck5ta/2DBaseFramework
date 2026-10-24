#pragma once

// Example game showing how the pieces fit together. docs/TUTORIAL.md builds this
// same game step by step (as MyGame). Replace it with your own: keep main.cpp,
// swap DemoGame for your Game subclass.

#include "core/Application.h"
#include "render2d/Camera2D.h"
#include "render2d/SpriteBatch.h"

#include <memory>
#include <random>
#include <vector>

class DemoGame : public core::Game {
public:
    void onInit(core::Application& app) override;
    void onShutdown(core::Application& app) override;
    void onFixedUpdate(core::Application& app, float dt) override;
    void onUpdate(core::Application& app, float dt) override;
    void onRender(core::Application& app, gfx::FrameContext& frame) override;

private:
    struct Particle {
        glm::vec2 position;
        glm::vec2 velocity;
        float life;
        float maxLife;
        render2d::Color color;
    };
    struct Coin {
        glm::vec2 position;
        float phase; // offsets the bobbing animation so coins don't move in sync
    };
    struct Popup {
        glm::vec2 position;
        float life; // 1 = just spawned, 0 = gone
    };

    void spawnBurst(glm::vec2 at, int count, render2d::Color color);
    glm::vec2 randomWorldPoint();

    std::unique_ptr<render2d::SpriteBatch> batch_;
    render2d::Camera2D camera_;
    gfx::Texture playerTexture_;
    gfx::Texture tileTexture_;
    gfx::Texture orbTexture_;
    render2d::Camera2D hudCamera_;
    render2d::Font font_;

    // Simulation state, advanced in onFixedUpdate. prevPlayerPos_ is the
    // position one step earlier, used to interpolate when rendering.
    glm::vec2 playerPos_{0.0f};
    glm::vec2 prevPlayerPos_{0.0f};
    glm::vec2 playerVel_{0.0f};
    glm::vec2 moveInput_{0.0f}; // written in onUpdate, read in onFixedUpdate
    bool facingLeft_ = false;

    std::vector<Particle> particles_;
    std::vector<Coin> coins_;
    int score_ = 0;
    std::mt19937 rng_{1234};
    std::vector<Popup> popups_;
    bool showHelp_ = true;
};
