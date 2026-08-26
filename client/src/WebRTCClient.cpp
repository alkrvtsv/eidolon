#include "WebRTCClient.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <rtc/h264rtpdepacketizer.hpp>

using json = nlohmann::json;

WebRTCClient::WebRTCClient() {}
WebRTCClient::~WebRTCClient() {}

bool WebRTCClient::Init() {
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.yandex.ru:3478");
    config.enableIceTcp = false;
    
    pc = std::make_shared<rtc::PeerConnection>(config);

    rtc::Description::Video media("video", rtc::Description::Direction::RecvOnly);
    media.addH264Codec(96);
    media.addSSRC(7777, "video-stream");
    
    videoTrack = pc->addTrack(media);

    videoTrack->onOpen([this]() {
        std::cout << "[WebRTC] Видео-канал успешно открыт! Ждем кадры..." << std::endl;
    });

    videoTrack->onMessage([this](std::variant<rtc::binary, std::string> data) {
        if (std::holds_alternative<rtc::binary>(data)) {
            auto& bin = std::get<rtc::binary>(data);
            const uint8_t* ptr = reinterpret_cast<const uint8_t*>(bin.data());
            size_t size = bin.size();

            if (onVideoData) {
                onVideoData(ptr, size);
            }
        }
    });

    pc->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[WebRTC] Состояние соединения: " << state << std::endl;
    });

    pc->onLocalCandidate([this](rtc::Candidate candidate) {
        json msg = {
            {"type", "ice_candidate"},
            {"candidate", candidate.candidate()},
            {"sdpMid", candidate.mid()}
        };
        if (onLocalCandidate) onLocalCandidate(msg.dump());
    });

    pc->onLocalDescription([this](rtc::Description description) {
        json msg = {
            {"type", description.typeString()},
            {"sdp", std::string(description)}
        };
        if (onLocalDescription) onLocalDescription(msg.dump());
    });

    std::cout << "[+] WebRTC Client успешно инициализирован." << std::endl;
    return true;
}

void WebRTCClient::CreateOffer() {
    std::cout << "[WebRTC] Создаем Offer для подключения к хосту..." << std::endl;
    pc->setLocalDescription(); 
}

void WebRTCClient::SetRemoteDescription(const std::string& type, const std::string& sdp) {
    std::cout << "[WebRTC] Применяем Remote Description типа: " << type << std::endl;
    pc->setRemoteDescription(rtc::Description(sdp, type));
}

void WebRTCClient::AddRemoteCandidate(const std::string& candidate, const std::string& mid) {
    pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
}