#include "protocol.h"
#include "spsc_queue.h"
#include "mmcss.h"
#include "capture/dxgi_capturer.h"
#include "color_converter/d3d11_video_processor.h"
#include "encoder/nvenc_encoder.h"
#include "audio/wasapi_opus_capturer.h"
#include "input/windows_input_injector.h"
#include "network/signaling_client.h"
#include "network/webrtc_streamer.h"
#include <iostream>
#include <thread>
#include <atomic>

int main() {
    MMCSSScopedTask mmcss(L"Games");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    DXGICapturer capturer;
    if (!capturer.Initialize()) {
        return -1;
    }

    D3D11VideoProcessorConverter converter;
    if (!converter.Initialize(capturer.GetDevice(), capturer.GetContext(), capturer.GetWidth(), capturer.GetHeight())) {
        return -1;
    }

    EncoderConfig encConfig;
    encConfig.width = capturer.GetWidth();
    encConfig.height = capturer.GetHeight();
    encConfig.frameRateNum = 60;
    encConfig.frameRateDen = 1;
    encConfig.bitRate = 15'000'000;
    encConfig.maxBitRate = 15'000'000;
    encConfig.vbvBufferSize = 1'500'000;
    encConfig.enableIntraRefresh = true;

    NVENCEncoder encoder;
    if (!encoder.Initialize(capturer.GetDevice(), encConfig)) {
        return -1;
    }

    WasapiOpusCapturer audioCapturer;
    if (!audioCapturer.Initialize()) {
        return -1;
    }

    WindowsInputInjector inputInjector;
    if (!inputInjector.Initialize()) {
        return -1;
    }

    WebRTCStreamer streamer;
    if (!streamer.Initialize()) {
        return -1;
    }

    SignalingClient signaling("ws://127.0.0.1:8080");
    signaling.SetOnMessageCallback([&](const std::string& msg) {
        streamer.ProcessSignalingMessage(msg);
    });

    streamer.SetSignalingSender([&](const std::string& msg) {
        signaling.SendMessage(msg);
    });

    encoder.SetEncodedFrameCallback([&](const uint8_t* data, size_t size) {
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

    std::atomic<bool> forceIDR{false};
    streamer.SetControlCallback([&](ControlCommandType cmd) {
        if (cmd == ControlCommandType::RequestIDR) {
            forceIDR = true;
        }
    });

    streamer.SetInputCallback([&](const uint8_t* data, size_t size) {
        if (size < sizeof(MessageType)) return;
        auto type = *reinterpret_cast<const MessageType*>(data);

        if (type == MessageType::InputMouseRelative && size >= sizeof(MouseRelativeMessage)) {
            const auto* msg = reinterpret_cast<const MouseRelativeMessage*>(data);
            inputInjector.InjectMouseRelative(msg->deltaX, msg->deltaY);
        } else if (type == MessageType::InputMouseButton && size >= sizeof(MouseButtonMessage)) {
            const auto* msg = reinterpret_cast<const MouseButtonMessage*>(data);
            inputInjector.InjectMouseButton(msg->button, msg->pressed != 0);
        } else if (type == MessageType::InputKeyboard && size >= sizeof(KeyboardMessage)) {
            const auto* msg = reinterpret_cast<const KeyboardMessage*>(data);
            inputInjector.InjectKeyboard(msg->vkCode, msg->pressed != 0);
        }
    });

    signaling.Connect();
    audioCapturer.Start();

    bool running = true;
    while (running) {
        ID3D11Texture2D* capturedTexture = nullptr;
        CaptureStatus status = capturer.AcquireFrame(&capturedTexture, 16);

        if (status == CaptureStatus::AccessLost) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            capturer.Initialize();
            converter.Initialize(capturer.GetDevice(), capturer.GetContext(), capturer.GetWidth(), capturer.GetHeight());
            encoder.Initialize(capturer.GetDevice(), encConfig);
            continue;
        }

        if (status == CaptureStatus::Success && capturedTexture) {
            ID3D11Texture2D* nv12Texture = nullptr;
            if (converter.Convert(capturedTexture, &nv12Texture)) {
                bool needIDR = forceIDR.exchange(false);
                encoder.EncodeFrame(nv12Texture, needIDR);
                nv12Texture->Release();
            }
            capturedTexture->Release();
            capturer.ReleaseFrame();
        }
    }

    audioCapturer.Stop();
    signaling.Disconnect();
    streamer.Shutdown();
    encoder.Shutdown();
    converter.Shutdown();
    capturer.Shutdown();

    return 0;
}