#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#define NOMINMAX
#include <windows.h>
#include <SDL2/SDL.h>
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>

#include "FFmpegDecoder.h"
#include "WebRTCClient.h"

using json = nlohmann::json;

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "=== Запуск Eidolon Client ===" << std::endl;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        std::cerr << "[-] Ошибка SDL: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Eidolon Stream (Client)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_BT709);
    SDL_Texture* texture = nullptr;

    FFmpegDecoder decoder;
    if (!decoder.Init()) return -1;

    WebRTCClient webrtc;
    if (!webrtc.Init()) return -1;

    // --- ПОТОКОБЕЗОПАСНАЯ ОЧЕРЕДЬ ПАКЕТОВ ---
    std::mutex queueMutex;
    std::vector<std::vector<uint8_t>> packetQueue;

    // Этот коллбек работает в фоновом сетевом потоке! Никакого рендера здесь.
    webrtc.onVideoData = [&](const uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lock(queueMutex);
        packetQueue.push_back(std::vector<uint8_t>(data, data + size));
    };

    rtc::WebSocket ws;
    std::atomic<bool> wsConnected(false);

    ws.onOpen([&]() {
        wsConnected = true;
        json reg = { {"type", "register"}, {"role", "client"} };
        ws.send(reg.dump());
        webrtc.CreateOffer();
    });

    webrtc.onLocalDescription = [&](const std::string& sdpStr) { if (wsConnected) ws.send(sdpStr); };
    webrtc.onLocalCandidate = [&](const std::string& candStr) { if (wsConnected) ws.send(candStr); };

    ws.onMessage([&](std::variant<rtc::binary, std::string> data) {
        if (std::holds_alternative<std::string>(data)) {
            try {
                auto msg = json::parse(std::get<std::string>(data));
                if (msg["type"] == "answer") webrtc.SetRemoteDescription(msg["type"], msg["sdp"]);
                else if (msg["type"] == "ice_candidate") webrtc.AddRemoteCandidate(msg["candidate"], msg["sdpMid"]);
            } catch (const std::exception& e) {
                std::cerr << "[-] Ошибка парсинга JSON: " << e.what() << std::endl;
            }
        }
    });

    std::string serverIp = "192.168.1.13"; // <--- ЗАМЕНИ НА LAN IP ТВОЕГО ХОСТА!
    if (argc > 1) {
        serverIp = argv[1];
    }
    
    std::string wsUrl = "ws://" + serverIp + ":8080";
    std::cout << "[WS] Подключение к " << wsUrl << "..." << std::endl;
    ws.open(wsUrl);

    bool running = true;
    SDL_Event event;

    // --- ГЛАВНЫЙ ЦИКЛ ПОТОКА (Main Thread) ---
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
        }

        // 1. Мгновенно забираем ВСЕ накопившиеся пакеты (чтобы не тормозить сеть)
        std::vector<std::vector<uint8_t>> localQueue;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            std::swap(localQueue, packetQueue); // O(1) операция, сеть сразу свободна!
        }

        // 2. Скармливаем их декодеру со скоростью пулемета
        bool frameUpdated = false;
        for (const auto& pkt : localQueue) {
            // Наш сборщик внутри DecodeAndRender сам поймет, когда куски сложатся в полный кадр
            if (decoder.DecodeAndRender(pkt.data(), pkt.size(), renderer, &texture)) {
                frameUpdated = true;
            }
        }

        // 3. Отрисовываем кадр на экран ТОЛЬКО если картинка реально обновилась
        if (frameUpdated) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);

            if (texture) {
                int texW, texH, winW, winH;
                SDL_QueryTexture(texture, nullptr, nullptr, &texW, &texH);
                SDL_GetWindowSize(window, &winW, &winH);

                // Вычисляем пропорции (Pillarboxing), чтобы окно не искажалось
                float scale = std::min((float)winW / texW, (float)winH / texH);
                
                SDL_Rect dest;
                dest.w = (int)(texW * scale);
                dest.h = (int)(texH * scale);
                dest.x = (winW - dest.w) / 2;
                dest.y = (winH - dest.h) / 2;

                SDL_RenderCopy(renderer, texture, nullptr, &dest);
            }
            SDL_RenderPresent(renderer);
        }

        // Крошечная пауза, чтобы не сжечь процессор. 
        // Так как мы выгребаем очередь пачками, задержек больше не будет!
        SDL_Delay(1); 
    }

    if (texture) SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}