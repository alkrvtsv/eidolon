#pragma once
#include <rtc/rtc.hpp>
#include <string>
#include <functional>
#include <memory>

class WebRTCClient {
public:
    WebRTCClient();
    ~WebRTCClient();

    bool Init();
    
    // Коллбеки для отправки JSON-сообщений на сигнальный сервер
    std::function<void(const std::string&)> onLocalDescription;
    std::function<void(const std::string&)> onLocalCandidate;
    
    // Коллбек, который срабатывает, когда приходят сырые байты H.264 видеопотока
    std::function<void(const uint8_t*, size_t)> onVideoData;

    // Методы для обработки ответов от сервера
    void SetRemoteDescription(const std::string& type, const std::string& sdp);
    void AddRemoteCandidate(const std::string& candidate, const std::string& mid);

    void CreateOffer();

private:
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> videoTrack;
};