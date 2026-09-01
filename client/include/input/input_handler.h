#pragma once

#include "protocol.h"
#include <SDL2/SDL.h>
#include <windows.h>
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
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    SDL_Window* window_{nullptr};
    bool mouseCaptured_{false};
    std::function<void(const uint8_t*, size_t)> inputCallback_;

    static inline HHOOK keyboardHook_{nullptr};
    static inline InputHandler* instance_{nullptr};
};