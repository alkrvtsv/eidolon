#pragma once

#include <cstdint>

class IInputInjector {
public:
    virtual ~IInputInjector() noexcept = default;

    virtual bool Initialize() = 0;
    virtual void InjectMouseRelative(int32_t deltaX, int32_t deltaY) = 0;
    virtual void InjectMouseAbsolute(uint16_t x, uint16_t y) = 0;
    virtual void InjectMouseButton(uint8_t button, bool pressed) = 0;
    virtual void InjectMouseWheel(int32_t deltaX, int32_t deltaY) = 0;
    virtual void InjectKeyboard(uint16_t vkCode, bool pressed) = 0;
};