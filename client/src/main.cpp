#include "protocol.h"
#include "mmcss.h"
#include "spsc_queue.h"
#include "decoder/ffmpeg_d3d11va_decoder.h"
#include "renderer/d3d11_renderer.h"
#include "audio/sdl_opus_player.h"
#include "input/input_handler.h"
#include "network/webrtc_client.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#include <iostream>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <windows.h>

using json = nlohmann::json;

struct EncodedVideoPacket {
    std::vector<uint8_t> data;
    uint64_t captureTimestampUs{0};
};

static void EnableHighDPI() {
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto setDpiAwareness = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext")
        );
        if (setDpiAwareness) {
            setDpiAwareness(reinterpret_cast<HANDLE>(-4));
        }
    }
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
}

struct CursorShapeEventData {
    CursorShapeMessage shape;
    uint8_t* pixels;
};

int main(int argc, char* argv[]) {
    EnableHighDPI();
    rtc::InitLogger(rtc::LogLevel::Warning);

    std::string signalingUrl = "ws://192.168.1.13:8080";
    if (argc > 1) {
        signalingUrl = argv[1];
        if (signalingUrl.rfind("ws://", 0) != 0) {
            signalingUrl = "ws://" + signalingUrl;
        }
    }
    std::cout << "[Client] Connecting to signaling server: " << signalingUrl << std::endl;

    std::cout << "[Client] Initializing SDL..." << std::endl;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) < 0) {
        std::cerr << "[Client ERROR] SDL_Init failed" << std::endl;
        return -1;
    }

    SDL_DisplayMode dm = {};
    uint32_t screenWidth = 1920;
    uint32_t screenHeight = 1080;
    uint32_t screenRefreshRate = 60;

    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        if (dm.w > 0 && dm.h > 0) {
            screenWidth = static_cast<uint32_t>(dm.w);
            screenHeight = static_cast<uint32_t>(dm.h);
        }
        if (dm.refresh_rate > 0) {
            screenRefreshRate = static_cast<uint32_t>(dm.refresh_rate);
        }
    }
    std::cout << "[Client] Detected display: " << screenWidth << "x" << screenHeight
              << " @" << screenRefreshRate << "Hz" << std::endl;

    Uint32 cursorShapeEventId = SDL_RegisterEvents(2);
    Uint32 cursorPositionEventId = cursorShapeEventId + 1;

    uint32_t windowWidth = screenWidth;
    uint32_t windowHeight = screenHeight;

    SDL_Window* window = SDL_CreateWindow(
        "Eidolon Stream Client",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        static_cast<int>(windowWidth),
        static_cast<int>(windowHeight),
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!window) {
        std::cerr << "[Client ERROR] SDL_CreateWindow failed" << std::endl;
        SDL_Quit();
        return -1;
    }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    HWND hwnd = wmInfo.info.win.window;

    RECT clientRect = {};
    GetClientRect(hwnd, &clientRect);
    windowWidth = clientRect.right - clientRect.left;
    windowHeight = clientRect.bottom - clientRect.top;

    D3D11Renderer renderer;
    if (!renderer.Initialize(hwnd, windowWidth, windowHeight)) {
        std::cerr << "[Client ERROR] Renderer Init Failed" << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    FFmpegD3D11VADecoder decoder;
    if (!decoder.Initialize(renderer.GetDevice(), renderer.GetContext())) {
        std::cerr << "[Client ERROR] Decoder Init Failed" << std::endl;
        renderer.Shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    SDLOpusPlayer audioPlayer;
    if (!audioPlayer.Initialize()) {
        std::cerr << "[Client WARNING] Audio Player Init Failed" << std::endl;
    }

    InputHandler inputHandler;
    inputHandler.Initialize(window);
    inputHandler.SetWindowSize(windowWidth, windowHeight);

    WebRTCClient client;
    if (!client.Initialize()) {
        std::cerr << "[Client ERROR] WebRTC Init Failed" << std::endl;
        decoder.Shutdown();
        renderer.Shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    ClientConfigMessage clientConfig;
    clientConfig.width = screenWidth;
    clientConfig.height = screenHeight;
    clientConfig.refreshRate = screenRefreshRate;
    clientConfig.maxBitrateKbps = 50000;
    client.SendClientConfig(clientConfig);

    PerformanceMetrics metrics;
    metrics.clientWidth = windowWidth;
    metrics.clientHeight = windowHeight;

    std::atomic<bool> isLogging{false};
    std::mutex logMutex;
    std::ofstream logFile;
    uint64_t loggedFrameIndex = 0;

    std::atomic<uint64_t> clientAssembledFrames{0};
    std::atomic<uint64_t> clientQueuePushFails{0};
    std::atomic<uint64_t> clientDecodedFrames{0};
    std::atomic<uint64_t> clientRenderedFrames{0};
    std::atomic<uint64_t> lastNetFrameReceiveMs{0};
    std::atomic<uint64_t> lastDecoderOutputMs{0};
    std::atomic<uint64_t> lastRenderOutputMs{0};

    std::chrono::high_resolution_clock::time_point decodeStartTime;
    decoder.SetFrameCallback([&](const DecodedFrame& frame) {
        clientDecodedFrames.fetch_add(1, std::memory_order_relaxed);
        lastDecoderOutputMs.store(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()), std::memory_order_relaxed);
        auto decodeEndTime = std::chrono::high_resolution_clock::now();
        metrics.decodeTimeMs = std::chrono::duration<float, std::milli>(decodeEndTime - decodeStartTime).count();

        inputHandler.SetHostResolution(frame.width, frame.height);
        metrics.hostWidth = frame.width;
        metrics.hostHeight = frame.height;

        metrics.bitrateMbps = client.GetIncomingBitrateMbps();
        metrics.lossRatePct = client.GetRecentLossRatePct();
        metrics.lostPacketsTotal = client.GetTotalLostPackets();

        auto t0 = std::chrono::high_resolution_clock::now();
        renderer.RenderFrame(frame, metrics);
        auto t1 = std::chrono::high_resolution_clock::now();
        metrics.renderTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

        if (isLogging.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lock(logMutex);
            if (logFile.is_open()) {
                loggedFrameIndex++;
                logFile << loggedFrameIndex << ","
                        << metrics.fps << ","
                        << metrics.bitrateMbps << ","
                        << metrics.frameSizeKb << ","
                        << metrics.lossRatePct << ","
                        << metrics.lostPacketsTotal << ","
                        << metrics.frameIntervalMs << ","
                        << metrics.frameJitterMs << ","
                        << metrics.waitLatencyMs << ","
                        << metrics.decodeTimeMs << ","
                        << metrics.bltTimeMs << ","
                        << metrics.presentTimeMs << ","
                        << metrics.renderTimeMs << ","
                        << metrics.videoQueueSize << "\n";
            }
        }
    });

    SPSCQueue<EncodedVideoPacket, 32> videoQueue;
    HANDLE videoEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::atomic<bool> renderRunning{true};
    std::atomic<bool> pendingResize{false};
    std::atomic<bool> toggleHudRequested{false};
    std::atomic<uint32_t> targetWidth{windowWidth};
    std::atomic<uint32_t> targetHeight{windowHeight};

    const float targetIntervalMs = 1000.0f / static_cast<float>(screenRefreshRate > 0 ? screenRefreshRate : 60);
    auto lastPacketReceiveTime = std::chrono::steady_clock::now();

    std::thread renderThread([&]() {
        MMCSSScopedTask mmcss(L"Games");
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        uint32_t frameCount = 0;
        auto lastFpsTime = std::chrono::steady_clock::now();

        while (renderRunning.load(std::memory_order_relaxed)) {
            if (pendingResize.exchange(false)) {
                uint32_t w = targetWidth.load();
                uint32_t h = targetHeight.load();
                renderer.Resize(w, h);
                metrics.clientWidth = w;
                metrics.clientHeight = h;
            }

            if (toggleHudRequested.exchange(false)) {
                renderer.ToggleHUD();
            }

            while (videoQueue.Empty() && renderRunning.load(std::memory_order_relaxed)) {
            DWORD waitRes = WaitForSingleObject(videoEvent, 16);
            if (waitRes == WAIT_TIMEOUT && client.IsConnected()) {
                auto nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                uint64_t lastNet = lastNetFrameReceiveMs.load(std::memory_order_relaxed);
                if (lastNet > 0 && nowMs > lastNet && (nowMs - lastNet) >= 500) {
                    static auto lastNetAlert = std::chrono::steady_clock::now();
                    auto nowAlert = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(nowAlert - lastNetAlert).count() >= 1000) {
                        std::cerr << "[Client ANOMALY] Network stall: no video packets for "
                                  << (nowMs - lastNet) << " ms | Assembled: "
                                  << clientAssembledFrames.load(std::memory_order_relaxed) << std::endl;
                        lastNetAlert = nowAlert;
                    }
                }
            }

            if (pendingResize.load(std::memory_order_relaxed) || toggleHudRequested.load(std::memory_order_relaxed)) {
                break;
            }
        }

        if (!renderRunning.load(std::memory_order_relaxed)) {
            break;
        }

        if (videoQueue.Empty()) {
            continue;
        }

        auto w0 = std::chrono::high_resolution_clock::now();
        renderer.WaitForFrameLatency(50);
        auto w1 = std::chrono::high_resolution_clock::now();
        metrics.waitLatencyMs = std::chrono::duration<float, std::milli>(w1 - w0).count();

        EncodedVideoPacket pkt;
        if (videoQueue.Pop(pkt)) {
            metrics.frameSizeKb = static_cast<float>(pkt.data.size()) / 1024.0f;
            metrics.videoQueueSize = videoQueue.Size();

            decodeStartTime = std::chrono::high_resolution_clock::now();
            decoder.Decode(pkt.data.data(), pkt.data.size(), true);
            frameCount++;
            clientRenderedFrames.fetch_add(1, std::memory_order_relaxed);
            lastRenderOutputMs.store(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()), std::memory_order_relaxed);
        } else {
            auto nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            uint64_t lastDec = lastDecoderOutputMs.load(std::memory_order_relaxed);
            if (lastDec > 0 && nowMs > lastDec && (nowMs - lastDec) >= 500) {
                static auto lastDecAlert = std::chrono::steady_clock::now();
                auto nowAlert = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(nowAlert - lastDecAlert).count() >= 1000) {
                    std::cerr << "[Client ANOMALY] Decoder/Render stall | QSize: " << videoQueue.Size()
                              << " | QPushFail: " << clientQueuePushFails.load(std::memory_order_relaxed)
                              << " | Decoded: " << clientDecodedFrames.load(std::memory_order_relaxed)
                              << " | Rendered: " << clientRenderedFrames.load(std::memory_order_relaxed) << std::endl;
                    lastDecAlert = nowAlert;
                }
            }
        }

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsTime).count();
            if (elapsed >= 1000) {
                metrics.fps = (frameCount * 1000.0f) / static_cast<float>(elapsed);
                frameCount = 0;
                lastFpsTime = now;
            }
        }
    });

    inputHandler.SetInputCallback([&](const uint8_t* data, size_t size) {
        client.SendInputData(data, size);
    });

    client.SetVideoCallback([&](const uint8_t* data, size_t size, uint64_t timestampUs) {
        auto now = std::chrono::steady_clock::now();
        float interval = std::chrono::duration<float, std::milli>(now - lastPacketReceiveTime).count();
        lastPacketReceiveTime = now;

        metrics.frameIntervalMs = interval;
        metrics.frameJitterMs = std::abs(interval - targetIntervalMs);

        clientAssembledFrames.fetch_add(1, std::memory_order_relaxed);
        lastNetFrameReceiveMs.store(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count()), std::memory_order_relaxed);

        EncodedVideoPacket pkt;
        pkt.data.assign(data, data + size);
        pkt.captureTimestampUs = timestampUs;

        if (videoQueue.Push(std::move(pkt))) {
            SetEvent(videoEvent);
        } else {
            clientQueuePushFails.fetch_add(1, std::memory_order_relaxed);
            SetEvent(videoEvent);
        }
    });

    client.SetAudioCallback([&](const uint8_t* data, size_t size) {
        audioPlayer.DecodeAndPlay(data, size);
    });

        client.SetCursorShapeCallback([&, cursorShapeEventId](const CursorShapeMessage& shape, const uint8_t* data) {
        if (!data || shape.width == 0 || shape.height == 0) return;
        if (shape.width > 256 || shape.height > 256) return;
        size_t expectedSize = static_cast<size_t>(shape.width) * shape.height * 4;
        if (shape.dataSize != expectedSize) return;
        auto* eventData = new CursorShapeEventData();
        eventData->shape = shape;
        eventData->pixels = new uint8_t[expectedSize];
        std::memcpy(eventData->pixels, data, expectedSize);
        SDL_Event event;
        SDL_zero(event);
        event.type = cursorShapeEventId;
        event.user.data1 = eventData;
        event.user.data2 = nullptr;
        SDL_PushEvent(&event);
    });
    client.SetCursorPositionCallback([&, cursorPositionEventId](const CursorPositionMessage& pos) {
        auto* posData = new CursorPositionMessage(pos);
        SDL_Event event;
        SDL_zero(event);
        event.type = cursorPositionEventId;
        event.user.data1 = posData;
        event.user.data2 = nullptr;
        SDL_PushEvent(&event);
    });

    rtc::WebSocket ws;
    ws.onOpen([&]() {
        std::cout << "[Client WS] Connected, registering as client..." << std::endl;
        json reg = { {"type", "register"}, {"role", "client"} };
        ws.send(reg.dump());
    });

    ws.onMessage([&](std::variant<rtc::binary, std::string> data) {
        if (std::holds_alternative<std::string>(data)) {
            client.ProcessSignalingMessage(std::get<std::string>(data));
        } else if (std::holds_alternative<rtc::binary>(data)) {
            const auto& bin = std::get<rtc::binary>(data);
            std::string str(reinterpret_cast<const char*>(bin.data()), bin.size());
            client.ProcessSignalingMessage(str);
        }
    });

    client.SetSignalingSender([&](const std::string& msg) {
        if (ws.isOpen()) ws.send(msg);
    });

    ws.open(signalingUrl);

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == cursorShapeEventId) {
                auto* eventData = static_cast<CursorShapeEventData*>(event.user.data1);
                if (eventData) {
                    inputHandler.UpdateCursorShape(eventData->shape, eventData->pixels);
                    delete[] eventData->pixels;
                    delete eventData;
                }
            } else if (event.type == cursorPositionEventId) {
                auto* posData = static_cast<CursorPositionMessage*>(event.user.data1);
                if (posData) {
                    inputHandler.UpdateCursorPosition(*posData);
                    delete posData;
                }
            } else if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN) {
                bool isCtrl = (SDL_GetModState() & KMOD_CTRL) != 0;
                bool isShift = (SDL_GetModState() & KMOD_SHIFT) != 0;

                if (isCtrl && isShift && event.key.keysym.scancode == SDL_SCANCODE_H) {
                    toggleHudRequested.store(true);
                    SetEvent(videoEvent);
                } else if (isCtrl && isShift && event.key.keysym.scancode == SDL_SCANCODE_L) {
                    if (!isLogging.load()) {
                        std::lock_guard<std::mutex> lock(logMutex);
                        logFile.open("perf_log.csv", std::ios::out | std::ios::trunc);
                        if (logFile.is_open()) {
                            logFile << "frame_index,fps,bitrate_mbps,frame_kb,loss_pct,lost_total,interval_ms,jitter_ms,wait_ms,decode_ms,blt_ms,present_ms,render_ms,queue_size\n";
                            loggedFrameIndex = 0;
                            isLogging.store(true);
                            std::cout << "[Client] Started CSV logging to perf_log.csv" << std::endl;
                        }
                    } else {
                        isLogging.store(false);
                        std::lock_guard<std::mutex> lock(logMutex);
                        if (logFile.is_open()) {
                            logFile.close();
                            std::cout << "[Client] Stopped CSV logging. Saved to perf_log.csv" << std::endl;
                        }
                    }
                } else {
                    inputHandler.ProcessEvent(event);
                }
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                RECT curRect = {};
                GetClientRect(hwnd, &curRect);
                uint32_t w = static_cast<uint32_t>(curRect.right - curRect.left);
                uint32_t h = static_cast<uint32_t>(curRect.bottom - curRect.top);
                inputHandler.SetWindowSize(w, h);
                targetWidth.store(w);
                targetHeight.store(h);
                pendingResize.store(true);
                SetEvent(videoEvent);
            } else {
                inputHandler.ProcessEvent(event);
            }
        }
        SDL_Delay(1);
    }

    renderRunning.store(false);
    SetEvent(videoEvent);
    if (renderThread.joinable()) {
        renderThread.join();
    }

    if (isLogging.load()) {
        isLogging.store(false);
        std::lock_guard<std::mutex> lock(logMutex);
        if (logFile.is_open()) {
            logFile.close();
        }
    }

    if (videoEvent) {
        CloseHandle(videoEvent);
    }

    ws.close();
    client.Shutdown();
    audioPlayer.Shutdown();
    decoder.Shutdown();
    renderer.Shutdown();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}