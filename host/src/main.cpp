#include <iostream>
#include <windows.h>
#include <thread>
#include <chrono>
#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include "DXGICapture.h"
#include "NVENCEncoder.h"    // <-- Подключаем кодировщик
#include "SignalingClient.h"
#include "WebRTCManager.h"

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

using json = nlohmann::json;

int main() {
    SetConsoleOutputCP(CP_UTF8);
    rtc::InitLogger(rtc::LogLevel::Error); 
    
    std::cout << "=== Запуск Low-Latency Host Worker ===" << std::endl;

    // 1. Инициализация DXGI
    DXGICapture capturer;
    if (!capturer.Init()) return -1;

    // 2. Инициализация NVENC (Связываем с DXGI)
    NVENCEncoder encoder;
    if (!encoder.Init(capturer.GetDevice(), capturer.GetWidth(), capturer.GetHeight())) {
        std::cerr << "[-] Критическая ошибка: Не удалось запустить NVENC." << std::endl;
        return -1;
    }

    // 3. WebRTC и Сеть
    WebRTCManager rtcManager;
    if (!rtcManager.Init()) return -1;

    SignalingClient signaling("ws://127.0.0.1:8080");

    // Клей WebRTC -> Signaling
    rtcManager.onLocalDescription = [&signaling](const std::string& msg) {
        signaling.SendMsg(msg);
    };
    rtcManager.onLocalCandidate = [&signaling](const std::string& msg) {
        signaling.SendMsg(msg);
    };

    // Клей Signaling -> WebRTC
    signaling.onMessageReceived = [&rtcManager](const std::string& msg) {
        try {
            json data = json::parse(msg);
            std::string type = data.value("type", "");
            if (type == "offer") {
                rtcManager.SetRemoteDescription(type, data["sdp"]);
            }
            else if (type == "ice_candidate") {
                rtcManager.AddRemoteCandidate(data["candidate"], data["sdpMid"]);
            }
        } 
        catch (const std::exception& e) {
            std::cerr << "[-] Ошибка при обработке сообщения: " << e.what() << std::endl;
        } 
        catch (...) {
            std::cerr << "[-] Критическая неизвестная ошибка в WebRTC!" << std::endl;
        }
    };

    signaling.Connect();
    std::cout << "[+] Ядро готово. Ожидание клиента..." << std::endl;
    
    while (true) {
        // 1. Берем и блокируем кадр
        ID3D11Texture2D* pTexture = capturer.AcquireFrame();
        
        if (pTexture) {
            // 2. Сжимаем заблокированный кадр (Zero-Copy)
            std::vector<uint8_t> encodedData = encoder.EncodeFrame(pTexture);
            
            // 3. Уничтожаем COM-указатель
            pTexture->Release();

            // 4. РАЗБЛОКИРУЕМ КАДР В DXGI! (Только после сжатия)
            capturer.UnlockFrame();

            // 5. Проверяем результат
            if (!encodedData.empty()) {
                std::cout << "[Video] Кадр сжат! Размер: " << encodedData.size() << " байт." << std::endl;
                
                // ОТПРАВЛЯЕМ КАДР КЛИЕНТУ!
                rtcManager.SendVideoData(encodedData);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    return 0;
}