#pragma once

#include "protocol.h"
#include <SDL2/SDL.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
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

    void SetWindowSize(uint32_t width, uint32_t height) {
        windowWidth_ = width;
        windowHeight_ = height;
    }

    void SetHostResolution(uint32_t width, uint32_t height) {
        hostWidth_ = width;
        hostHeight_ = height;
    }

    void UpdateCursorPosition(const CursorPositionMessage& pos);
    void UpdateCursorShape(const CursorShapeMessage& shape, const uint8_t* rgbaData);

    void SetMouseCaptured(bool captured);
    bool IsMouseCaptured() const { return mouseCaptured_; }

private:
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    SDL_Window* window_{nullptr};
    uint32_t windowWidth_{1920};
    uint32_t windowHeight_{1080};
    uint32_t hostWidth_{2560};
    uint32_t hostHeight_{1440};

    bool mouseCaptured_{false};
    bool hostCursorVisible_{true};
    std::function<void(const uint8_t*, size_t)> inputCallback_;

    SDL_Cursor* currentSdlCursor_{nullptr};

    static inline HHOOK keyboardHook_{nullptr};
    static inline InputHandler* instance_{nullptr};
};