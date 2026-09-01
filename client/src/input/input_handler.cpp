#include "input/input_handler.h"
#include <windows.h>
#include <iostream>

InputHandler::InputHandler() = default;
InputHandler::~InputHandler() noexcept = default;

bool InputHandler::Initialize(SDL_Window* window) {
    window_ = window;
    SetMouseCaptured(true);
    return true;
}

void InputHandler::SetMouseCaptured(bool captured) {
    mouseCaptured_ = captured;
    if (window_) {
        SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
    }
    std::cout << "[Input] Mouse " << (captured ? "CAPTURED (Press Shift+Escape to release)" : "RELEASED (Click in window to capture)") << std::endl;
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

        case SDL_SCANCODE_RETURN: return VK_RETURN;
        case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
        case SDL_SCANCODE_BACKSPACE: return VK_BACK;
        case SDL_SCANCODE_TAB: return VK_TAB;
        case SDL_SCANCODE_SPACE: return VK_SPACE;

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

        default: return 0;
    }
}

void InputHandler::ProcessEvent(const SDL_Event& event) {
    if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (!mouseCaptured_) {
            SetMouseCaptured(true);
            return;
        }

        if (inputCallback_) {
            MouseButtonMessage msg;
            msg.type = MessageType::InputMouseButton;
            msg.button = event.button.button;
            msg.pressed = 1;
            inputCallback_(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
        }
    } else if (event.type == SDL_MOUSEBUTTONUP) {
        if (mouseCaptured_ && inputCallback_) {
            MouseButtonMessage msg;
            msg.type = MessageType::InputMouseButton;
            msg.button = event.button.button;
            msg.pressed = 0;
            inputCallback_(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
        }
    } else if (event.type == SDL_MOUSEMOTION) {
        if (mouseCaptured_ && inputCallback_) {
            if (event.motion.xrel != 0 || event.motion.yrel != 0) {
                MouseRelativeMessage msg;
                msg.type = MessageType::InputMouseRelative;
                msg.deltaX = event.motion.xrel;
                msg.deltaY = event.motion.yrel;
                inputCallback_(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
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