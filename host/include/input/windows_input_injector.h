#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "input/input_injector.h"
#include <windows.h>

class WindowsInputInjector final : public IInputInjector {
public:
    WindowsInputInjector();
    ~WindowsInputInjector() noexcept override;

    bool Initialize() override;
    void Shutdown() noexcept override;

    void InjectMouseRelative(int32_t deltaX, int32_t deltaY) override;
    void InjectMouseButton(uint8_t button, bool pressed) override;
    void InjectKeyboard(uint16_t vkCode, bool pressed) override;

private:
    HANDLE driverHandle_{INVALID_HANDLE_VALUE};
    bool useKernelDriver_{false};

    bool TryOpenKernelDriver();
};