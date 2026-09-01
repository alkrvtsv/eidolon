#pragma once

#include "protocol.h"
#include <SDL2/SDL.h>
#include <functional>
#include <cstdint>

class InputHandler {
public:
    InputHandler();
    ~InputHandler() noexcept;

    bool Initialize(SDL_Window* window);
    void ProcessEvent(const SDL_Event& event);

    void SetInputCallback(std::function<void(const uint8_t*, size_t)> callback) {
        inputCallback_ = std::move(callback);
    }

    void SetMouseCaptured(bool captured);
    bool IsMouseCaptured() const { return mouseCaptured_; }

private:
    SDL_Window* window_{nullptr};
    bool mouseCaptured_{false};
    std::function<void(const uint8_t*, size_t)> inputCallback_;
};