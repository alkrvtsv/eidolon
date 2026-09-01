#include "protocol.h"
#include "mmcss.h"
#include "capture/dxgi_capturer.h"
#include "color_converter/d3d11_video_processor.h"
#include "encoder/nvenc_encoder.h"
#include "audio/wasapi_opus_capturer.h"
#include "input/windows_input_injector.h"
#include "network/signaling_client.h"
#include "network/webrtc_streamer.h"
#include <rtc/rtc.hpp>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <windows.h>

int main() {
    rtc::InitLogger(rtc::LogLevel::Warning);

    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

    MMCSSScopedTask mmcss(L"Games");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    std::cout << "[Host] Initializing DXGI Capturer..." << std::endl;
    DXGICapturer capturer;
    if (!capturer.Initialize()) {
        std::cerr << "[Host ERROR] Capturer failed!" << std::endl;
        return -1;
    }
    std::cout << "[Host] Capturer OK: " << capturer.GetWidth() << "x" << capturer.GetHeight() << std::endl;

    D3D11VideoProcessorConverter converter;
    if (!converter.Initialize(capturer.GetDevice(), capturer.GetContext(), capturer.GetWidth(), capturer.GetHeight())) {
        std::cerr << "[Host ERROR] Converter failed!" << std::endl;
        return -1;
    }

    EncoderConfig encConfig;
    encConfig.width = capturer.GetWidth();
    encConfig.height = capturer.GetHeight();
    encConfig.frameRateNum = 60;
    encConfig.frameRateDen = 1;
    encConfig.bitRate = 20'000'000;
    encConfig.maxBitRate = 25'000'000;
    encConfig.vbvBufferSize = 2'000'000;
    encConfig.enableIntraRefresh = false;

    NVENCEncoder encoder;
    if (!encoder.Initialize(capturer.GetDevice(), encConfig)) {
        std::cerr << "[Host ERROR] NVENC Init failed!" << std::endl;
        return -1;
    }

    WindowsInputInjector inputInjector;
    if (!inputInjector.Initialize()) {
        std::cerr << "[Host WARNING] WindowsInputInjector failed to initialize" << std::endl;
    }

    WasapiOpusCapturer audioCapturer;
    if (!audioCapturer.Initialize()) {
        std::cerr << "[Host WARNING] Audio capturer failed to initialize" << std::endl;
    }

    WebRTCStreamer streamer;
    if (!streamer.Initialize()) {
        std::cerr << "[Host ERROR] WebRTC Init failed!" << std::endl;
        return -1;
    }

    SignalingClient signaling("ws://127.0.0.1:8080");
    signaling.SetOnMessageCallback([&](const std::string& msg) {
        streamer.ProcessSignalingMessage(msg);
    });

    streamer.SetSignalingSender([&](const std::string& msg) {
        signaling.SendText(msg);
    });

    std::atomic<bool> forceIDR{true};
    streamer.SetControlCallback([&](ControlCommandType cmd) {
        if (cmd == ControlCommandType::RequestIDR) {
            std::cout << "[Host] IDR Keyframe & Cursor resend requested" << std::endl;
            forceIDR = true;
            capturer.ResendCursorState();
        }
    });

    streamer.SetInputCallback([&](const uint8_t* data, size_t size) {
        if (size < sizeof(MessageType)) return;
        auto type = *reinterpret_cast<const MessageType*>(data);

        if (type == MessageType::InputMouseAbsolute && size >= sizeof(MouseAbsoluteMessage)) {
            const auto* msg = reinterpret_cast<const MouseAbsoluteMessage*>(data);
            inputInjector.InjectMouseAbsolute(msg->x, msg->y);
        } else if (type == MessageType::InputMouseRelative && size >= sizeof(MouseRelativeMessage)) {
            const auto* msg = reinterpret_cast<const MouseRelativeMessage*>(data);
            inputInjector.InjectMouseRelative(msg->deltaX, msg->deltaY);
        } else if (type == MessageType::InputMouseButton && size >= sizeof(MouseButtonMessage)) {
            const auto* msg = reinterpret_cast<const MouseButtonMessage*>(data);
            inputInjector.InjectMouseButton(msg->button, msg->pressed != 0);
        } else if (type == MessageType::InputMouseWheel && size >= sizeof(MouseWheelMessage)) {
            const auto* msg = reinterpret_cast<const MouseWheelMessage*>(data);
            inputInjector.InjectMouseWheel(msg->deltaX, msg->deltaY);
        } else if (type == MessageType::InputKeyboard && size >= sizeof(KeyboardMessage)) {
            const auto* msg = reinterpret_cast<const KeyboardMessage*>(data);
            inputInjector.InjectKeyboard(msg->vkCode, msg->pressed != 0);
        }
    });

    std::atomic<uint64_t> sentFrames{0};
    encoder.SetEncodedFrameCallback([&](const uint8_t* data, size_t size) {
        sentFrames++;
        if (sentFrames == 1 || sentFrames % 120 == 0) {
            std::cout << "[Host Pipeline] Sent Frame #" << sentFrames << " (" << size << " bytes)" << std::endl;
        }
        streamer.SendVideoFrame(data, size);
    });

    audioCapturer.SetEncodedAudioCallback([&](const uint8_t* data, size_t size) {
        streamer.SendAudioFrame(data, size);
    });

    capturer.SetCursorShapeCallback([&](const CursorShapeMessage& shape, const uint8_t* data) {
        streamer.SendCursorShape(shape, data);
    });

    capturer.SetCursorPositionCallback([&](const CursorPositionMessage& pos) {
        streamer.SendCursorPosition(pos);
    });

    ComPtr<ID3D11Texture2D> lastValidNV12;

    // Гарантированный захват первого валидного кадра экрана до старта сети
    std::cout << "[Host] Waiting for initial desktop frame..." << std::endl;
    while (!lastValidNV12) {
        ID3D11Texture2D* capturedTexture = nullptr;
        CaptureStatus status = capturer.AcquireFrame(&capturedTexture, 50);
        if (status == CaptureStatus::Success && capturedTexture) {
            ID3D11Texture2D* nv12 = nullptr;
            if (converter.Convert(capturedTexture, &nv12)) {
                lastValidNV12.Attach(nv12);
            }
            capturedTexture->Release();
            capturer.ReleaseFrame();
        } else {
            // Микро-тик ввода для побуждения DWM выдать начальный кадр
            INPUT in = {};
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_MOVE;
            SendInput(1, &in, sizeof(INPUT));
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    std::cout << "[Host] Initial desktop frame captured OK!" << std::endl;

    signaling.Connect();
    audioCapturer.Start();
    std::cout << "[Host] Pipeline ready and running..." << std::endl;

    auto lastFrameTime = std::chrono::steady_clock::now();

    while (true) {
        ID3D11Texture2D* capturedTexture = nullptr;
        CaptureStatus status = capturer.AcquireFrame(&capturedTexture, 16);

        if (status == CaptureStatus::AccessLost) {
            std::cout << "[Host WARNING] Access Lost -> Retrying..." << std::endl;
            while (!capturer.Initialize()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            std::cout << "[Host] Desktop restored: " << capturer.GetWidth() << "x" << capturer.GetHeight() << std::endl;
            converter.Initialize(capturer.GetDevice(), capturer.GetContext(), capturer.GetWidth(), capturer.GetHeight());
            encConfig.width = capturer.GetWidth();
            encConfig.height = capturer.GetHeight();
            encoder.Initialize(capturer.GetDevice(), encConfig);
            forceIDR = true;
            continue;
        }

        auto now = std::chrono::steady_clock::now();

        if (status == CaptureStatus::Success && capturedTexture) {
            ID3D11Texture2D* nv12Texture = nullptr;
            if (converter.Convert(capturedTexture, &nv12Texture)) {
                lastValidNV12.Reset();
                lastValidNV12.Attach(nv12Texture);

                bool needIDR = forceIDR.exchange(false);
                encoder.EncodeFrame(lastValidNV12.Get(), needIDR);
                lastFrameTime = now;
            }
            capturedTexture->Release();
            capturer.ReleaseFrame();
        } else {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();
            if (lastValidNV12 && (forceIDR.load() || elapsedMs >= 16)) {
                bool needIDR = forceIDR.exchange(false);
                encoder.EncodeFrame(lastValidNV12.Get(), needIDR);
                lastFrameTime = now;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    return 0;
}