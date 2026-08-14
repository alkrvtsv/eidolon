#pragma once
#include <rtc/rtc.hpp>
#include <memory>
#include <string>
#include <functional>
#include <iostream>

class WebRTCManager {
public:
    WebRTCManager();
    ~WebRTCManager();

    bool Init();

    // Методы для обработки входящих данных от Клиента
    void SetRemoteDescription(const std::string& type, const std::string& sdp);
    void AddRemoteCandidate(const std::string& candidate, const std::string& mid);

    // Коллбеки для отправки наших данных наружу
    std::function<void(const std::string&)> onLocalDescription;
    std::function<void(const std::string&)> onLocalCandidate;

    void SendVideoData(const std::vector<uint8_t>& data);

private:
    std::shared_ptr<rtc::PeerConnection> pc;
    std::atomic<std::shared_ptr<rtc::Track>> videoTrack;
};