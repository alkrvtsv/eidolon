#include "input/input_handler.h"
#include <algorithm>
#include <iostream>

InputHandler::InputHandler() {
    instance_ = this;
}

InputHandler::~InputHandler() noexcept {
    SetMouseCaptured(false);
    if (currentSdlCursor_) {
        SDL_FreeCursor(currentSdlCursor_);
        currentSdlCursor_ = nullptr;
    }
    instance_ = nullptr;
}

bool InputHandler::Initialize(SDL_Window* window) {
    window_ = window;
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    windowWidth_ = static_cast<uint32_t>(w);
    windowHeight_ = static_cast<uint32_t>(h);

    SetMouseCaptured(true);
    return true;
}

void InputHandler::SetMouseCaptured(bool captured) {
    mouseCaptured_ = captured;
    if (window_) {
        if (captured) {
            SDL_SetWindowGrab(window_, SDL_TRUE);
            SDL_SetWindowKeyboardGrab(window_, SDL_TRUE);
            if (!hostCursorVisible_) {
                SDL_SetRelativeMouseMode(SDL_TRUE);
            } else {
                SDL_SetRelativeMouseMode(SDL_FALSE);
                SDL_ShowCursor(SDL_ENABLE);
            }
        } else {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_ShowCursor(SDL_ENABLE);
            SDL_SetWindowGrab(window_, SDL_FALSE);
            SDL_SetWindowKeyboardGrab(window_, SDL_FALSE);
        }
    }

    if (captured) {
        if (!keyboardHook_) {
            keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
        }
        std::cout << "[Input] Captured (Shift+Escape to release, Dual-Mode Active)" << std::endl;
    } else {
        if (keyboardHook_) {
            UnhookWindowsHookEx(keyboardHook_);
            keyboardHook_ = nullptr;
        }
        std::cout << "[Input] Released (Click inside window to focus)" << std::endl;
    }
}

void InputHandler::UpdateCursorPosition(const CursorPositionMessage& pos) {
    bool newVisible = (pos.visible != 0);

    // Переключаем режим SDL ТОЛЬКО когда статус видимости реально изменился (вход в 3D игру)
    if (newVisible != hostCursorVisible_) {
        hostCursorVisible_ = newVisible;
        if (mouseCaptured_ && window_) {
            if (!hostCursorVisible_) {
                // 3D игра скрыла курсор: захватываем дельты
                SDL_SetRelativeMouseMode(SDL_TRUE);
            } else {
                // Курсор снова на экране: возвращаем локальный курсор без скачков
                SDL_SetRelativeMouseMode(SDL_FALSE);
                SDL_ShowCursor(SDL_ENABLE);
            }
        }
    }
}

void InputHandler::UpdateCursorShape(const CursorShapeMessage& shape, const uint8_t* rgbaData) {
    if (!rgbaData || shape.width == 0 || shape.height == 0) return;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<void*>(reinterpret_cast<const void*>(rgbaData)),
        shape.width,
        shape.height,
        32,
        shape.width * 4,
        SDL_PIXELFORMAT_BGRA32
    );

    if (surface) {
        SDL_Cursor* newCursor = SDL_CreateColorCursor(surface, shape.hotspotX, shape.hotspotY);
        if (newCursor) {
            SDL_SetCursor(newCursor);
            if (currentSdlCursor_) {
                SDL_FreeCursor(currentSdlCursor_);
            }
            currentSdlCursor_ = newCursor;
        }
        SDL_FreeSurface(surface);
    }
}

LRESULT CALLBACK InputHandler::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && instance_ && instance_->mouseCaptured_) {
        auto* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        if (kbd->vkCode == VK_LWIN || kbd->vkCode == VK_RWIN) {
            bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            if (instance_->inputCallback_) {
                KeyboardMessage msg;
                msg.type = MessageType::InputKeyboard;
                msg.vkCode = static_cast<uint16_t>(kbd->vkCode);
                msg.pressed = isDown ? 1 : 0;
                instance_->inputCallback_(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
            }
            return 1;
        }
    }
    return CallNextHookEx(keyboardHook_, nCode, wParam, lParam);
}

static uint16_t SDLScancodeToVK(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_A: return 'A';
        case SDL_SCANCODE_B: return 'B';
        case SDL_SCANCODE_C: return 'C';
        case SDL_SCANCODE_D: return 'D';
        case SDL_SCANCODE_E: return 'E';
        case SDL_SCANCODE_F: return 'F';
        case SDL_SCANCODE_G: return 'G';
        case SDL_SCANCODE_H: return 'H';
        case SDL_SCANCODE_I: return 'I';
        case SDL_SCANCODE_J: return 'J';
        case SDL_SCANCODE_K: return 'K';
        case SDL_SCANCODE_L: return 'L';
        case SDL_SCANCODE_M: return 'M';
        case SDL_SCANCODE_N: return 'N';
        case SDL_SCANCODE_O: return 'O';
        case SDL_SCANCODE_P: return 'P';
        case SDL_SCANCODE_Q: return 'Q';
        case SDL_SCANCODE_R: return 'R';
        case SDL_SCANCODE_S: return 'S';
        case SDL_SCANCODE_T: return 'T';
        case SDL_SCANCODE_U: return 'U';
        case SDL_SCANCODE_V: return 'V';
        case SDL_SCANCODE_W: return 'W';
        case SDL_SCANCODE_X: return 'X';
        case SDL_SCANCODE_Y: return 'Y';
        case SDL_SCANCODE_Z: return 'Z';

        case SDL_SCANCODE_1: return '1';
        case SDL_SCANCODE_2: return '2';
        case SDL_SCANCODE_3: return '3';
        case SDL_SCANCODE_4: return '4';
        case SDL_SCANCODE_5: return '5';
        case SDL_SCANCODE_6: return '6';
        case SDL_SCANCODE_7: return '7';
        case SDL_SCANCODE_8: return '8';
        case SDL_SCANCODE_9: return '9';
        case SDL_SCANCODE_0: return '0';

        case SDL_SCANCODE_COMMA: return VK_OEM_COMMA;
        case SDL_SCANCODE_PERIOD: return VK_OEM_PERIOD;
        case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
        case SDL_SCANCODE_APOSTROPHE: return VK_OEM_7;
        case SDL_SCANCODE_LEFTBRACKET: return VK_OEM_4;
        case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
        case SDL_SCANCODE_GRAVE: return VK_OEM_3;
        case SDL_SCANCODE_SLASH: return VK_OEM_2;
        case SDL_SCANCODE_BACKSLASH: return VK_OEM_5;
        case SDL_SCANCODE_MINUS: return VK_OEM_MINUS;
        case SDL_SCANCODE_EQUALS: return VK_OEM_PLUS;

        case SDL_SCANCODE_RETURN: return VK_RETURN;
        case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
        case SDL_SCANCODE_BACKSPACE: return VK_BACK;
        case SDL_SCANCODE_TAB: return VK_TAB;
        case SDL_SCANCODE_SPACE: return VK_SPACE;
        case SDL_SCANCODE_CAPSLOCK: return VK_CAPITAL;

        case SDL_SCANCODE_LCTRL: return VK_LCONTROL;
        case SDL_SCANCODE_LSHIFT: return VK_LSHIFT;
        case SDL_SCANCODE_LALT: return VK_LMENU;
        case SDL_SCANCODE_LGUI: return VK_LWIN;
        case SDL_SCANCODE_RCTRL: return VK_RCONTROL;
        case SDL_SCANCODE_RSHIFT: return VK_RSHIFT;
        case SDL_SCANCODE_RALT: return VK_RMENU;
        case SDL_SCANCODE_RGUI: return VK_RWIN;

        case SDL_SCANCODE_UP: return VK_UP;
        case SDL_SCANCODE_DOWN: return VK_DOWN;
        case SDL_SCANCODE_LEFT: return VK_LEFT;
        case SDL_SCANCODE_RIGHT: return VK_RIGHT;
        case SDL_SCANCODE_INSERT: return VK_INSERT;
        case SDL_SCANCODE_DELETE: return VK_DELETE;
        case SDL_SCANCODE_HOME: return VK_HOME;
        case SDL_SCANCODE_END: return VK_END;
        case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
        case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;

        case SDL_SCANCODE_F1: return VK_F1;
        case SDL_SCANCODE_F2: return VK_F2;
        case SDL_SCANCODE_F3: return VK_F3;
        case SDL_SCANCODE_F4: return VK_F4;
        case SDL_SCANCODE_F5: return VK_F5;
        case SDL_SCANCODE_F6: return VK_F6;
        case SDL_SCANCODE_F7: return VK_F7;
        case SDL_SCANCODE_F8: return VK_F8;
        case SDL_SCANCODE_F9: return VK_F9;
        case SDL_SCANCODE_F10: return VK_F10;
        case SDL_SCANCODE_F11: return VK_F11;
        case SDL_SCANCODE_F12: return VK_F12;

        case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
        case SDL_SCANCODE_KP_1: return VK_NUMPAD1;
        case SDL_SCANCODE_KP_2: return VK_NUMPAD2;
        case SDL_SCANCODE_KP_3: return VK_NUMPAD3;
        case SDL_SCANCODE_KP_4: return VK_NUMPAD4;
        case SDL_SCANCODE_KP_5: return VK_NUMPAD5;
        case SDL_SCANCODE_KP_6: return VK_NUMPAD6;
        case SDL_SCANCODE_KP_7: return VK_NUMPAD7;
        case SDL_SCANCODE_KP_8: return VK_NUMPAD8;
        case SDL_SCANCODE_KP_9: return VK_NUMPAD9;
        case SDL_SCANCODE_KP_DIVIDE: return VK_DIVIDE;
        case SDL_SCANCODE_KP_MULTIPLY: return VK_MULTIPLY;
        case SDL_SCANCODE_KP_MINUS: return VK_SUBTRACT;
        case SDL_SCANCODE_KP_PLUS: return VK_ADD;
        case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
        case SDL_SCANCODE_KP_PERIOD: return VK_DECIMAL;
        case SDL_SCANCODE_NUMLOCKCLEAR: return VK_NUMLOCK;
        case SDL_SCANCODE_SCROLLLOCK: return VK_SCROLL;
        case SDL_SCANCODE_PRINTSCREEN: return VK_SNAPSHOT;
        case SDL_SCANCODE_PAUSE: return VK_PAUSE;

        default: return 0;
    }
}

void InputHandler::ProcessEvent(const SDL_Event& event) {
    if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (!mouseCaptured_) {
            SetMouseCaptured(true);
        }

        if (inputCallback_) {
            MouseButtonMessage msg;
            msg.type = MessageType::InputMouseButton;
            msg.button = event.button.button;
            msg.pressed = 1;
            inputCallback_(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
        }
    } else if (event.type == SDL_MOUSEBUTTONUP) {
        if (inputCallback_) {
            MouseButtonMessage msg;
            msg.type = MessageType::InputMouseButton;
            msg.button = event.button.button;
            msg.pressed = 0;
            inputCallback_(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
        }
    } else if (event.type == SDL_MOUSEMOTION) {
        if (mouseCaptured_ && inputCallback_) {
            if (hostCursorVisible_) {
                int clampedX = std::clamp(event.motion.x, 0, static_cast<int>(windowWidth_));
                int clampedY = std::clamp(event.motion.y, 0, static_cast<int>(windowHeight_));

                MouseAbsoluteMessage msg;
                msg.type = MessageType::InputMouseAbsolute;
                msg.x = static_cast<uint16_t>((static_cast<uint64_t>(clampedX) * 65535) / windowWidth_);
                msg.y = static_cast<uint16_t>((static_cast<uint64_t>(clampedY) * 65535) / windowHeight_);
                inputCallback_(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
            } else {
                if (event.motion.xrel != 0 || event.motion.yrel != 0) {
                    MouseRelativeMessage msg;
                    msg.type = MessageType::InputMouseRelative;
                    msg.deltaX = event.motion.xrel;
                    msg.deltaY = event.motion.yrel;
                    inputCallback_(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
                }
            }
        }
    } else if (event.type == SDL_MOUSEWHEEL) {
        if (mouseCaptured_ && inputCallback_) {
            MouseWheelMessage msg;
            msg.type = MessageType::InputMouseWheel;
            msg.deltaX = event.wheel.x;
            msg.deltaY = event.wheel.y;
            inputCallback_(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
        }
    } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE && (SDL_GetModState() & KMOD_SHIFT)) {
            SetMouseCaptured(false);
            return;
        }

        if (mouseCaptured_ && inputCallback_) {
            uint16_t vk = SDLScancodeToVK(event.key.keysym.scancode);
            if (vk != 0) {
                KeyboardMessage msg;
                msg.type = MessageType::InputKeyboard;
                msg.vkCode = vk;
                msg.pressed = (event.type == SDL_KEYDOWN) ? 1 : 0;
                inputCallback_(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
            }
        }
    }
}