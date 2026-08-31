#include "input/windows_input_injector.h"

WindowsInputInjector::WindowsInputInjector() = default;

WindowsInputInjector::~WindowsInputInjector() noexcept {
    Shutdown();
}

bool WindowsInputInjector::Initialize() {
    Shutdown();
    useKernelDriver_ = TryOpenKernelDriver();
    return true;
}

void WindowsInputInjector::Shutdown() noexcept {
    if (driverHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(driverHandle_);
        driverHandle_ = INVALID_HANDLE_VALUE;
    }
    useKernelDriver_ = false;
}

bool WindowsInputInjector::TryOpenKernelDriver() {
    driverHandle_ = CreateFileW(
        L"\\\\.\\FakerInput",
        GENERIC_WRITE | GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    return driverHandle_ != INVALID_HANDLE_VALUE;
}

void WindowsInputInjector::InjectMouseRelative(int32_t deltaX, int32_t deltaY) {
    if (deltaX == 0 && deltaY == 0) {
        return;
    }

    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(deltaX);
    input.mi.dy = static_cast<LONG>(deltaY);
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;
    input.mi.time = 0;
    input.mi.dwExtraInfo = 0;

    SendInput(1, &input, sizeof(INPUT));
}

void WindowsInputInjector::InjectMouseButton(uint8_t button, bool pressed) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.time = 0;
    input.mi.dwExtraInfo = 0;

    switch (button) {
        case 1:
            input.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;
        case 2:
            input.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;
        case 3:
            input.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
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

    SendInput(1, &input, sizeof(INPUT));
}

void WindowsInputInjector::InjectKeyboard(uint16_t vkCode, bool pressed) {
    if (vkCode == 0) {
        return;
    }

    UINT scanCode = MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC);

    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 0;
    input.ki.wScan = static_cast<WORD>(scanCode);
    input.ki.dwFlags = KEYEVENTF_SCANCODE;
    
    if (!pressed) {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }

    if ((scanCode & 0xFF00) == 0xE000 || (scanCode & 0xFF00) == 0xE100) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }

    input.ki.time = 0;
    input.ki.dwExtraInfo = 0;

    SendInput(1, &input, sizeof(INPUT));
}