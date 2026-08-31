#pragma once

#include "protocol.h"
#include <cstdint>

class IInputInjector {
public:
    virtual ~IInputInjector() noexcept = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() noexcept = 0;

    virtual void InjectMouseRelative(int32_t deltaX, int32_t deltaY) = 0;
    virtual void InjectMouseButton(uint8_t button, bool pressed) = 0;
    virtual void InjectKeyboard(uint16_t vkCode, bool pressed) = 0;
};