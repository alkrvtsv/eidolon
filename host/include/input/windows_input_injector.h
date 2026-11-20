#pragma once

#include "input/input_injector.h"
#include <windows.h>
#include <unordered_set>

class WindowsInputInjector final : public IInputInjector {
public:
    WindowsInputInjector();
    ~WindowsInputInjector() noexcept override;

    bool Initialize() override;
    void InjectMouseRelative(int32_t deltaX, int32_t deltaY) override;
    void InjectMouseAbsolute(uint16_t x, uint16_t y) override;
    void InjectMouseButton(uint8_t button, bool pressed) override;
    void InjectMouseWheel(int32_t deltaX, int32_t deltaY) override;
    void InjectKeyboard(uint16_t vkCode, bool pressed) override;
    void ReleaseAll() noexcept;

private:
    std::unordered_set<uint16_t> pressedKeys_;
    std::unordered_set<uint8_t> pressedMouseButtons_;
};