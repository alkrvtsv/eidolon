#include "input/windows_input_injector.h"

WindowsInputInjector::WindowsInputInjector() = default;

WindowsInputInjector::~WindowsInputInjector() noexcept {
    ReleaseAll();
}

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

void WindowsInputInjector::InjectMouseAbsolute(uint16_t x, uint16_t y) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(x);
    input.mi.dy = static_cast<LONG>(y);
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &input, sizeof(INPUT));
}

void WindowsInputInjector::InjectMouseButton(uint8_t button, bool pressed) {
    INPUT input = {};
    input.type = INPUT_MOUSE;

    switch (button) {
    case 1:
        input.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        break;
    case 2:
        input.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        break;
    case 3:
        input.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        break;
    case 4:
        input.mi.dwFlags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
        input.mi.mouseData = XBUTTON1;
        break;
    case 5:
        input.mi.dwFlags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
        input.mi.mouseData = XBUTTON2;
        break;
    default:
        return;
    }

    if (pressed) {
        pressedMouseButtons_.insert(button);
    } else {
        pressedMouseButtons_.erase(button);
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
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = pressed ? 0 : KEYEVENTF_KEYUP;

    if ((vkCode >= VK_PRIOR && vkCode <= VK_DOWN) ||
        vkCode == VK_INSERT || vkCode == VK_DELETE ||
        vkCode == VK_LWIN || vkCode == VK_RWIN ||
        vkCode == VK_RMENU || vkCode == VK_RCONTROL ||
        vkCode == VK_DIVIDE || vkCode == VK_NUMLOCK) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }

    if (pressed) {
        pressedKeys_.insert(vkCode);
    } else {
        pressedKeys_.erase(vkCode);
    }

    SendInput(1, &input, sizeof(INPUT));
}

void WindowsInputInjector::ReleaseAll() noexcept {
    for (uint16_t vk : pressedKeys_) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        if ((vk >= VK_PRIOR && vk <= VK_DOWN) ||
            vk == VK_INSERT || vk == VK_DELETE ||
            vk == VK_LWIN || vk == VK_RWIN ||
            vk == VK_RMENU || vk == VK_RCONTROL ||
            vk == VK_DIVIDE || vk == VK_NUMLOCK) {
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        SendInput(1, &input, sizeof(INPUT));
    }
    pressedKeys_.clear();

    for (uint8_t btn : pressedMouseButtons_) {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        switch (btn) {
        case 1: input.mi.dwFlags = MOUSEEVENTF_LEFTUP; break;
        case 2: input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP; break;
        case 3: input.mi.dwFlags = MOUSEEVENTF_RIGHTUP; break;
        case 4: input.mi.dwFlags = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON1; break;
        case 5: input.mi.dwFlags = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON2; break;
        default: continue;
        }
        SendInput(1, &input, sizeof(INPUT));
    }
    pressedMouseButtons_.clear();
}