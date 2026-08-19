#include <iostream>
#include <windows.h>
#include <thread>
#include <chrono>
#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include "DXGICapture.h"
#include "NVENCEncoder.h"    
#include "SignalingClient.h"
#include "WebRTCManager.h"

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

using json = nlohmann::json;

int main() {
    rtc::InitLogger(rtc::LogLevel::Verbose);
    SetConsoleOutputCP(CP_UTF8);
    rtc::InitLogger(rtc::LogLevel::Error); 
    
    std::cout << "=== Запуск Low-Latency Host Worker ===" << std::endl;

    DXGICapture capturer;
    if (!capturer.Init()) return -1;

    NVENCEncoder encoder;
    if (!encoder.Init(capturer.GetDevice(), capturer.GetWidth(), capturer.GetHeight())) {
        std::cerr << "[-] Критическая ошибка: Не удалось запустить NVENC." << std::endl;
        return -1;
    }

    WebRTCManager rtcManager;
    if (!rtcManager.Init()) return -1;

    SignalingClient signaling("ws://127.0.0.1:8080");

    rtcManager.onLocalDescription = [&signaling](const std::string& msg) {
        signaling.SendMsg(msg);
    };
    rtcManager.onLocalCandidate = [&signaling](const std::string& msg) {
        signaling.SendMsg(msg);
    };

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
        ID3D11Texture2D* pTexture = capturer.AcquireFrame();
        
        if (pTexture) {
            std::vector<uint8_t> encodedData = encoder.EncodeFrame(pTexture);
            
            pTexture->Release();

            capturer.UnlockFrame();

            if (!encodedData.empty()) {
                rtcManager.SendVideoData(encodedData);
            }
        }
    }

    return 0;
}