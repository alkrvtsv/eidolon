#pragma once

#include <cstdint>

#pragma pack(push, 1)

enum class MessageType : uint8_t {
    InputMouseRelative = 0x01,
    InputMouseButton   = 0x02,
    InputKeyboard      = 0x03,
    CursorShape        = 0x10,
    CursorPosition     = 0x11,
    ControlCommand     = 0x20
};

enum class ControlCommandType : uint8_t {
    RequestIDR         = 0x01
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

struct KeyboardMessage {
    MessageType type{MessageType::InputKeyboard};
    uint16_t vkCode{0};
    uint8_t pressed{0};
};

struct CursorShapeMessage {
    MessageType type{MessageType::CursorShape};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t hotspotX{0};
    uint32_t hotspotY{0};
    uint32_t dataSize{0};
};

struct CursorPositionMessage {
    MessageType type{MessageType::CursorPosition};
    int32_t x{0};
    int32_t y{0};
    uint8_t visible{0};
};

struct ControlCommandMessage {
    MessageType type{MessageType::ControlCommand};
    ControlCommandType command{ControlCommandType::RequestIDR};
};

#pragma pack(pop)