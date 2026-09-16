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
#include <algorithm>
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

int main() {
    try {
        rtc::InitLogger(rtc::LogLevel::Warning);

        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

        MMCSSScopedTask mmcss(L"Games");
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        timeBeginPeriod(1);

        std::cout << "[Host] Initializing DXGI Capturer..." << std::endl;
        DXGICapturer capturer;
        if (!capturer.Initialize()) {
            std::cerr << "[Host ERROR] Capturer failed!" << std::endl;
            timeEndPeriod(1);
            return -1;
        }
        std::cout << "[Host] Capturer OK: " << capturer.GetWidth() << "x" << capturer.GetHeight() << std::endl;

        D3D11VideoProcessorConverter converter;
        if (!converter.Initialize(capturer.GetDevice(), capturer.GetContext(), capturer.GetWidth(), capturer.GetHeight())) {
            std::cerr << "[Host ERROR] Converter failed!" << std::endl;
            timeEndPeriod(1);
            return -1;
        }

        std::atomic<uint32_t> sessionFps{60};
        std::atomic<uint32_t> sessionBitrate{35'000'000};
        std::atomic<int64_t> sessionMinIntervalUs{10000};
        std::atomic<bool> reconfigureEncoderRequested{false};

        EncoderConfig encConfig;
        encConfig.width = capturer.GetWidth();
        encConfig.height = capturer.GetHeight();
        encConfig.frameRateNum = sessionFps.load();
        encConfig.frameRateDen = 1;
        encConfig.bitRate = sessionBitrate.load();
        encConfig.maxBitRate = static_cast<uint32_t>(sessionBitrate.load() * 1.25);
        encConfig.vbvBufferSize = static_cast<uint32_t>(sessionBitrate.load() / (encConfig.frameRateNum ? encConfig.frameRateNum : 120) * 1.5);
        encConfig.enableIntraRefresh = true;
        encConfig.intraRefreshPeriod = encConfig.frameRateNum * 2;
        encConfig.intraRefreshDuration = encConfig.frameRateNum / 2;

        NVENCEncoder encoder;
        if (!encoder.Initialize(capturer.GetDevice(), encConfig)) {
            std::cerr << "[Host ERROR] NVENC Init failed!" << std::endl;
            timeEndPeriod(1);
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
            timeEndPeriod(1);
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
        auto lastIdrRequestTime = std::chrono::steady_clock::now();

        streamer.SetControlCallback([&](ControlCommandType cmd) {
            if (cmd == ControlCommandType::RequestIDR) {
                auto now = std::chrono::steady_clock::now();
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastIdrRequestTime).count();
                if (elapsedMs >= 200) {
                    forceIDR.store(true);
                    capturer.ResendCursorState();
                    lastIdrRequestTime = now;
                }
            }
        });

        streamer.SetClientConfigCallback([&](const ClientConfigMessage& cfg) {
            std::cout << "[Host] Received ClientConfig Handshake: " << cfg.width << "x" << cfg.height
                      << " @" << cfg.refreshRate << "Hz, Max Bitrate: " << cfg.maxBitrateKbps << " kbps" << std::endl;

            uint32_t targetFps = (cfg.refreshRate > 0) ? cfg.refreshRate : 60;
            targetFps = std::clamp(targetFps, 30u, 144u);
            sessionFps.store(targetFps);

            uint32_t targetBps = (cfg.maxBitrateKbps > 0) ? (cfg.maxBitrateKbps * 1000) : 35'000'000;
            sessionBitrate.store(targetBps);

            int64_t targetIntervalUs = static_cast<int64_t>((1'000'000.0 / targetFps) * 0.85);
            sessionMinIntervalUs.store(targetIntervalUs);

            reconfigureEncoderRequested.store(true);
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
        std::atomic<uint64_t> currentCaptureTimestampUs{0};

        encoder.SetEncodedFrameCallback([&](const uint8_t* data, size_t size) {
            sentFrames++;
            if (sentFrames == 1 || sentFrames % 120 == 0) {
                std::cout << "[Host Pipeline] Sent Frame #" << sentFrames << " (" << size << " bytes)" << std::endl;
            }
            if (!streamer.SendVideoFrame(data, size, currentCaptureTimestampUs.load(std::memory_order_relaxed))) {
                forceIDR = true;
            }
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

        constexpr auto kKeepAliveInterval = std::chrono::milliseconds(100);
        auto lastEncodeTime = std::chrono::steady_clock::now();

        while (true) {
            if (reconfigureEncoderRequested.exchange(false)) {
                uint32_t fps = sessionFps.load();
                uint32_t bitrate = sessionBitrate.load();

                encConfig.frameRateNum = fps;
                encConfig.frameRateDen = 1;
                encConfig.bitRate = bitrate;
                encConfig.maxBitRate = static_cast<uint32_t>(bitrate * 1.25);
                encConfig.vbvBufferSize = static_cast<uint32_t>(bitrate / (fps ? fps : 120) * 1.5);
                encConfig.enableIntraRefresh = true;
                encConfig.intraRefreshPeriod = fps * 2;
                encConfig.intraRefreshDuration = fps / 2;

                std::cout << "[Host] Soft Intra-Refresh WAN Pipeline: " << fps << " FPS, "
                          << (bitrate / 1'000'000) << " Mbps (Duration: " 
                          << encConfig.intraRefreshDuration << " frames)" << std::endl;

                encoder.Shutdown();
                encoder.Initialize(capturer.GetDevice(), encConfig);
                forceIDR = true;
            }

            uint32_t acquireTimeoutMs = 2;

            ID3D11Texture2D* capturedTexture = nullptr;
            CaptureStatus status = capturer.AcquireFrame(&capturedTexture, acquireTimeoutMs);

            if (status == CaptureStatus::AccessLost || status == CaptureStatus::Error) {
                std::cout << "[Host WARNING] DXGI Access Lost -> Reinitializing pipeline..." << std::endl;
                lastValidNV12.Reset();
                capturer.Shutdown();

                while (!capturer.Initialize()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }

                std::cout << "[Host] Desktop restored: " << capturer.GetWidth() << "x" << capturer.GetHeight() << std::endl;
                converter.Shutdown();
                converter.Initialize(capturer.GetDevice(), capturer.GetContext(), capturer.GetWidth(), capturer.GetHeight());

                encConfig.width = capturer.GetWidth();
                encConfig.height = capturer.GetHeight();
                encoder.Shutdown();
                encoder.Initialize(capturer.GetDevice(), encConfig);

                forceIDR = true;
                continue;
            }

            auto now = std::chrono::steady_clock::now();

            if (status == CaptureStatus::Success && capturedTexture) {
                auto elapsedSinceLast = std::chrono::duration_cast<std::chrono::microseconds>(now - lastEncodeTime);
                if (elapsedSinceLast.count() < sessionMinIntervalUs.load(std::memory_order_relaxed)) {
                    capturedTexture->Release();
                    capturer.ReleaseFrame();
                    continue;
                }

                auto nowUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
                currentCaptureTimestampUs.store(nowUs, std::memory_order_relaxed);

                ID3D11Texture2D* nv12Texture = nullptr;
                if (converter.Convert(capturedTexture, &nv12Texture)) {
                    lastValidNV12.Reset();
                    lastValidNV12.Attach(nv12Texture);
                }
                capturedTexture->Release();
                capturer.ReleaseFrame();

                if (lastValidNV12) {
                    bool needIDR = forceIDR.exchange(false);
                    encoder.EncodeFrame(lastValidNV12.Get(), needIDR);
                    lastEncodeTime = now;
                }
            } else if (status == CaptureStatus::Timeout) {
                auto elapsedSinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastEncodeTime);
                if (elapsedSinceLast >= kKeepAliveInterval && lastValidNV12) {
                    auto nowUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
                    currentCaptureTimestampUs.store(nowUs, std::memory_order_relaxed);

                    bool needIDR = forceIDR.exchange(false);
                    encoder.EncodeFrame(lastValidNV12.Get(), needIDR);
                    lastEncodeTime = now;
                }
            }
        }

        timeEndPeriod(1);
    } catch (const std::exception& e) {
        std::cerr << "[Host FATAL] Exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[Host FATAL] Unknown unhandled exception caught!" << std::endl;
    }

    return 0;
}