#include "input/windows_input_injector.h"

WindowsInputInjector::WindowsInputInjector() = default;
WindowsInputInjector::~WindowsInputInjector() noexcept = default;

bool WindowsInputInjector::Initialize() {
    return true;
}

void WindowsInputInjector::InjectMouseRelative(int32_t deltaX, int32_t deltaY) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = deltaX;
    input.mi.dy = deltaY;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

void WindowsInputInjector::InjectMouseButton(uint8_t button, bool pressed) {
    INPUT input = {};
    input.type = INPUT_MOUSE;

    switch (button) {
        case 1: // Left
            input.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;
        case 2: // Middle
            input.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            break;
        case 3: // Right
            input.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;
        case 4: // XBUTTON1
            input.mi.dwFlags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = XBUTTON1;
            break;
        case 5: // XBUTTON2
            input.mi.dwFlags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = XBUTTON2;
            break;
        default:
            return;
    }

    SendInput(1, &input, sizeof(INPUT));
}

void WindowsInputInjector::InjectMouseWheel(int32_t deltaX, int32_t deltaY) {
    if (deltaY != 0) {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(deltaY * WHEEL_DELTA);
        SendInput(1, &input, sizeof(INPUT));
    }
    if (deltaX != 0) {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        input.mi.mouseData = static_cast<DWORD>(deltaX * WHEEL_DELTA);
        SendInput(1, &input, sizeof(INPUT));
    }
}

void WindowsInputInjector::InjectKeyboard(uint16_t vkCode, bool pressed) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vkCode;
    input.ki.dwFlags = pressed ? 0 : KEYEVENTF_KEYUP;

    // Для расширенных клавиш (стрелки, Insert/Delete и т.д.)
    if (vkCode >= VK_PRIOR && vkCode <= VK_DOWN) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }

    SendInput(1, &input, sizeof(INPUT));
}