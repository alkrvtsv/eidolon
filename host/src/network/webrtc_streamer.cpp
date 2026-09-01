#include "network/webrtc_streamer.h"
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

WebRTCStreamer::WebRTCStreamer() {
    cursorPayloadBuffer_.reserve(64 * 64 * 4 + sizeof(CursorShapeMessage));
}

WebRTCStreamer::~WebRTCStreamer() noexcept {
    Shutdown();
}

bool WebRTCStreamer::Initialize() {
    return true;
}

void WebRTCStreamer::CreatePeerConnection() {
    peerConnected_ = false;
    hasRemoteDescription_ = false;
    pendingCandidates_.clear();

    if (pc_) {
        pc_->close();
        pc_.reset();
    }

    rtc::Configuration config;
    config.enableIceTcp = false;

    pc_ = std::make_shared<rtc::PeerConnection>(config);

    pc_->onLocalDescription([this](rtc::Description desc) {
        std::cout << "[WebRTC Host] Local Description (" << desc.typeString() << ") -> Signaling" << std::endl;
        if (signalingSend_) {
            json msg = { {"type", desc.typeString()}, {"sdp", std::string(desc)} };
            signalingSend_(msg.dump());
        }
    });

    pc_->onLocalCandidate([this](rtc::Candidate cand) {
        if (signalingSend_) {
            json msg = { {"type", "ice_candidate"}, {"candidate", cand.candidate()}, {"sdpMid", cand.mid()} };
            signalingSend_(msg.dump());
        }
    });

    pc_->onStateChange([this](rtc::PeerConnection::State state) {
        std::cout << "[WebRTC Host] State: " << state << std::endl;
        peerConnected_ = (state == rtc::PeerConnection::State::Connected);
        if (peerConnected_ && controlCallback_) {
            controlCallback_(ControlCommandType::RequestIDR);
        }
    });

    SetupDataChannels();
}

void WebRTCStreamer::SetupDataChannels() {
    // Видео-канал: ненадежная доставка без повторов (UDP-like)
    rtc::DataChannelInit videoInit;
    videoInit.reliability.unordered = true;
    videoInit.reliability.maxRetransmits = 0;
    videoChannel_ = pc_->createDataChannel("video", videoInit);

    // Канал ввода: без ограничений по времени жизни пакетов
    rtc::DataChannelInit inputInit;
    inputInit.reliability.unordered = true;
    inputInit.reliability.maxRetransmits = 0;
    inputChannel_ = pc_->createDataChannel("input", inputInit);

    inputChannel_->onMessage([this](std::variant<rtc::binary, std::string> data) {
        if (std::holds_alternative<rtc::binary>(data) && inputCallback_) {
            const auto& bin = std::get<rtc::binary>(data);
            inputCallback_(reinterpret_cast<const uint8_t*>(bin.data()), bin.size());
        }
    });

    rtc::DataChannelInit audioInit;
    audioInit.reliability.unordered = true;
    audioInit.reliability.maxPacketLifeTime = std::chrono::milliseconds(100);
    audioChannel_ = pc_->createDataChannel("audio", audioInit);

    rtc::DataChannelInit cursorInit;
    cursorInit.reliability.unordered = false;
    cursorChannel_ = pc_->createDataChannel("cursor", cursorInit);

    rtc::DataChannelInit controlInit;
    controlInit.reliability.unordered = false;
    controlChannel_ = pc_->createDataChannel("control", controlInit);

    controlChannel_->onMessage([this](std::variant<rtc::binary, std::string> data) {
        if (std::holds_alternative<rtc::binary>(data) && controlCallback_) {
            const auto& bin = std::get<rtc::binary>(data);
            if (bin.size() >= sizeof(ControlCommandMessage)) {
                const auto* cmd = reinterpret_cast<const ControlCommandMessage*>(bin.data());
                controlCallback_(cmd->command);
            }
        }
    });
}

void WebRTCStreamer::StartSession() {
    CreatePeerConnection();
}

void WebRTCStreamer::Shutdown() noexcept {
    peerConnected_ = false;
    hasRemoteDescription_ = false;
    pendingCandidates_.clear();

    if (videoChannel_) { videoChannel_->close(); videoChannel_.reset(); }
    if (inputChannel_) { inputChannel_->close(); inputChannel_.reset(); }
    if (audioChannel_) { audioChannel_->close(); audioChannel_.reset(); }
    if (cursorChannel_) { cursorChannel_->close(); cursorChannel_.reset(); }
    if (controlChannel_) { controlChannel_->close(); controlChannel_.reset(); }
    if (pc_) { pc_->close(); pc_.reset(); }
}

void WebRTCStreamer::ProcessSignalingMessage(const std::string& msg) {
    try {
        auto data = json::parse(msg);
        std::string type = data.value("type", "");

        if (type == "start_session") {
            std::cout << "[WebRTC Host] Client connected -> Starting session" << std::endl;
            StartSession();
        } else if (type == "answer") {
            std::cout << "[WebRTC Host] Setting remote Answer" << std::endl;
            pc_->setRemoteDescription(rtc::Description(data["sdp"], type));
            hasRemoteDescription_ = true;

            for (const auto& [cand, mid] : pendingCandidates_) {
                pc_->addRemoteCandidate(rtc::Candidate(cand, mid));
            }
            pendingCandidates_.clear();
        } else if (type == "ice_candidate") {
            std::string cand = data["candidate"];
            std::string mid = data["sdpMid"];

            if (hasRemoteDescription_) {
                pc_->addRemoteCandidate(rtc::Candidate(cand, mid));
            } else {
                pendingCandidates_.emplace_back(cand, mid);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[WebRTC Host] Signaling Error: " << e.what() << std::endl;
    }
}

void WebRTCStreamer::SendVideoFrame(const uint8_t* data, size_t size) {
    if (peerConnected_ && videoChannel_ && videoChannel_->isOpen() && data && size > 0) {
        rtc::binary frame(reinterpret_cast<const std::byte*>(data), reinterpret_cast<const std::byte*>(data + size));
        videoChannel_->send(std::move(frame));
    }
}

void WebRTCStreamer::SendAudioFrame(const uint8_t* data, size_t size) {
    if (peerConnected_ && audioChannel_ && audioChannel_->isOpen() && data && size > 0) {
        rtc::binary frame(reinterpret_cast<const std::byte*>(data), reinterpret_cast<const std::byte*>(data + size));
        audioChannel_->send(std::move(frame));
    }
}

void WebRTCStreamer::SendCursorShape(const CursorShapeMessage& shape, const uint8_t* data) {
    if (!peerConnected_ || !cursorChannel_ || !cursorChannel_->isOpen()) return;

    size_t totalSize = sizeof(CursorShapeMessage) + shape.dataSize;
    if (cursorPayloadBuffer_.size() < totalSize) {
        cursorPayloadBuffer_.resize(totalSize);
    }

    memcpy(cursorPayloadBuffer_.data(), &shape, sizeof(CursorShapeMessage));
    if (data && shape.dataSize > 0) {
        memcpy(cursorPayloadBuffer_.data() + sizeof(CursorShapeMessage), data, shape.dataSize);
    }

    rtc::binary payload(
        reinterpret_cast<const std::byte*>(cursorPayloadBuffer_.data()),
        reinterpret_cast<const std::byte*>(cursorPayloadBuffer_.data() + totalSize)
    );
    cursorChannel_->send(std::move(payload));
}

void WebRTCStreamer::SendCursorPosition(const CursorPositionMessage& pos) {
    if (peerConnected_ && cursorChannel_ && cursorChannel_->isOpen()) {
        rtc::binary payload(
            reinterpret_cast<const std::byte*>(&pos),
            reinterpret_cast<const std::byte*>(&pos) + sizeof(CursorPositionMessage)
        );
        cursorChannel_->send(std::move(payload));
    }
}