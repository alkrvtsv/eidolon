#include "WebRTCManager.h"
#include <nlohmann/json.hpp>
#include <rtc/rtppacketizationconfig.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rtc/h264rtppacketizer.hpp>

using json = nlohmann::json;

WebRTCManager::WebRTCManager() {}
WebRTCManager::~WebRTCManager() {}

bool WebRTCManager::Init() {
    // 1. Настройка конфигурации сети для WebRTC
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    
    // 2. Создаем ядро соединения
    pc = std::make_shared<rtc::PeerConnection>(config);

    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96); 
    media.addSSRC(7777, "video-stream"); 
    videoTrack = pc->addTrack(media);

    // СТАЛО:
    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(7777, "video", 96, 90000);
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::H264RtpPacketizer::Separator::LongStartSequence, rtpConfig);
    
    videoTrack->setMediaHandler(packetizer);

    // 3. Коллбек: Изменение состояния соединения
    pc->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[WebRTC] Состояние соединения изменилось: " << state << std::endl;
    });

    // 4. Коллбек: Когда WebRTC находит наш локальный/публичный IP (ICE Candidate)
    pc->onLocalCandidate([this](rtc::Candidate candidate) {
        // Упаковываем кандидата в JSON для отправки клиенту
        json msg = {
            {"type", "ice_candidate"},
            {"candidate", candidate.candidate()},
            {"sdpMid", candidate.mid()}
        };
        // Передаем сформированную строку наверх (в SignalingClient)
        if (onLocalCandidate) {
            onLocalCandidate(msg.dump());
        }
    });

    // 5. Коллбек: Когда WebRTC формирует SDP Offer или Answer (Описание медиапотока)
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
    std::cout << "[WebRTC] Применяем Remote Description типа: " << type << std::endl;
    
    // Передаем параметры клиента в ядро WebRTC
    pc->setRemoteDescription(rtc::Description(sdp, type));
    
    // Если клиент прислал "offer", мы должны сгенерировать "answer"
    if (type == "offer") {
        std::cout << "[WebRTC] Offer получен. Генерируем Answer..." << std::endl;
        // libdatachannel автоматически создаст answer, если вызвать этот метод без аргументов
        pc->setLocalDescription(); 
    }
}

void WebRTCManager::AddRemoteCandidate(const std::string& candidate, const std::string& mid) {
    // Добавляем возможный сетевой маршрут к клиенту
    pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
    std::cout << "[WebRTC] Добавлен Remote ICE Candidate." << std::endl;
}

void WebRTCManager::SendVideoData(const std::vector<uint8_t>& data) {
    // Если клиент подключился и канал открыт - отправляем байты!
    if (videoTrack && videoTrack->isOpen()) {
        // Конвертируем наш вектор uint8_t во внутренний формат rtc::binary
        rtc::binary sample(
            reinterpret_cast<const std::byte*>(data.data()), 
            reinterpret_cast<const std::byte*>(data.data() + data.size())
        );
        videoTrack->send(sample);
    }
}