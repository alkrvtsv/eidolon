#include "protocol.h"
#include "mmcss.h"
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
#include <string>

using json = nlohmann::json;

int main(int argc, char* argv[]) {
    rtc::InitLogger(rtc::LogLevel::Warning);

    MMCSSScopedTask mmcss(L"Games");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

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

    uint32_t windowWidth = 1920;
    uint32_t windowHeight = 1080;

    SDL_Window* window = SDL_CreateWindow(
        "Eidolon Stream Client",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        static_cast<int>(windowWidth),
        static_cast<int>(windowHeight),
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
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

    WebRTCClient client;
    if (!client.Initialize()) {
        std::cerr << "[Client ERROR] WebRTC Init Failed" << std::endl;
        decoder.Shutdown();
        renderer.Shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    inputHandler.SetInputCallback([&](const uint8_t* data, size_t size) {
        client.SendInputData(data, size);
    });

    client.SetVideoCallback([&](const uint8_t* data, size_t size) {
        decoder.Decode(data, size);
    });

    client.SetAudioCallback([&](const uint8_t* data, size_t size) {
        audioPlayer.DecodeAndPlay(data, size);
    });

    client.SetCursorShapeCallback([&](const CursorShapeMessage& shape, const uint8_t* data) {
        inputHandler.UpdateCursorShape(shape, data);
    });

    client.SetCursorPositionCallback([&](const CursorPositionMessage& pos) {
        inputHandler.UpdateCursorPosition(pos);
    });

    decoder.SetFrameCallback([&](const DecodedFrame& frame) {
        inputHandler.SetHostResolution(frame.width, frame.height);
        renderer.RenderFrame(frame);
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
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                uint32_t w = static_cast<uint32_t>(event.window.data1);
                uint32_t h = static_cast<uint32_t>(event.window.data2);
                renderer.Resize(w, h);
                inputHandler.SetWindowSize(w, h);
            } else {
                inputHandler.ProcessEvent(event);
            }
        }
        SDL_Delay(1);
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