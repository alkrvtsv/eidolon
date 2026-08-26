#include "WebRTCManager.h"
#include <nlohmann/json.hpp>
#include <rtc/rtppacketizationconfig.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rtc/h264rtppacketizer.hpp>

using json = nlohmann::json;

WebRTCManager::WebRTCManager() {}
WebRTCManager::~WebRTCManager() {}

bool WebRTCManager::Init() {
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.yandex.ru:3478");
    config.enableIceTcp = false;
    
    pc = std::make_shared<rtc::PeerConnection>(config);

    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96); 
    media.addSSRC(7777, "video-stream"); 
    
    auto track = pc->addTrack(media);

    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(7777, "video", 96, 90000);
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::H264RtpPacketizer::Separator::LongStartSequence, rtpConfig);
    
    track->setMediaHandler(packetizer);
    
    videoTrack.store(track);

    pc->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[WebRTC] Состояние соединения изменилось: " << state << std::endl;
    });

    pc->onLocalCandidate([this](rtc::Candidate candidate) {
        json msg = {
            {"type", "ice_candidate"},
            {"candidate", candidate.candidate()},
            {"sdpMid", candidate.mid()}
        };
        if (onLocalCandidate) {
            onLocalCandidate(msg.dump());
        }
    });

    pc->onLocalDescription([this](rtc::Description description) {
        json msg = {
            {"type", description.typeString()},
            {"sdp", std::string(description)}
        };
        if (onLocalDescription) {
            onLocalDescription(msg.dump());
        }
    });

    std::cout << "[+] WebRTC PeerConnection успешно инициализирован." << std::endl;
    return true;
}

void WebRTCManager::SetRemoteDescription(const std::string& type, const std::string& sdp) {
    if (type == "offer") {
        std::cout << "[WebRTC] Новый клиент (или переподключение)! Перезапускаем сессию..." << std::endl;
        if (pc) {
            pc->close(); 
        }
        Init(); 
    }

    std::cout << "[WebRTC] Применяем Remote Description типа: " << type << std::endl;
    pc->setRemoteDescription(rtc::Description(sdp, type));
    
    if (type == "offer") {
        std::cout << "[WebRTC] Offer получен. Генерируем Answer..." << std::endl;
        pc->setLocalDescription(); 
    }
}

void WebRTCManager::AddRemoteCandidate(const std::string& candidate, const std::string& mid) {
    pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
    std::cout << "[WebRTC] Добавлен Remote ICE Candidate." << std::endl;
}

void WebRTCManager::SendVideoData(const std::vector<uint8_t>& data) {
    auto track = videoTrack.load(); 
    if (track && track->isOpen()) {
        rtc::binary sample(
            reinterpret_cast<const std::byte*>(data.data()), 
            reinterpret_cast<const std::byte*>(data.data() + data.size())
        );
        track->send(sample);
    }
}