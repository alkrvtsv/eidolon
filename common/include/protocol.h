#pragma once

#include <cstdint>

#pragma pack(push, 1)

enum class MessageType : uint8_t {
    InputMouseRelative = 1,
    InputMouseButton = 2,
    InputMouseWheel = 3,
    InputKeyboard = 4,
    CursorPosition = 5,
    CursorShape = 6,
    ControlCommand = 7
};

struct MouseRelativeMessage {
    MessageType type{MessageType::InputMouseRelative};
    int32_t deltaX{0};
    int32_t deltaY{0};
};

struct MouseButtonMessage {
    MessageType type{MessageType::InputMouseButton};
    uint8_t button{0};
    uint8_t pressed{0};
};

struct MouseWheelMessage {
    MessageType type{MessageType::InputMouseWheel};
    int32_t deltaX{0};
    int32_t deltaY{0};
};

struct KeyboardMessage {
    MessageType type{MessageType::InputKeyboard};
    uint16_t vkCode{0};
    uint8_t pressed{0};
};

struct CursorPositionMessage {
    MessageType type{MessageType::CursorPosition};
    int32_t x{0};
    int32_t y{0};
    uint8_t visible{1};
};

struct CursorShapeMessage {
    MessageType type{MessageType::CursorShape};
    uint32_t width{0};
    uint32_t height{0};
    int32_t hotspotX{0};
    int32_t hotspotY{0};
    uint32_t dataSize{0};
};

enum class ControlCommandType : uint8_t {
    RequestIDR = 1,
    KeepAlive = 2
};

struct ControlCommandMessage {
    MessageType type{MessageType::ControlCommand};
    ControlCommandType command{ControlCommandType::RequestIDR};
};

#pragma pack(pop)