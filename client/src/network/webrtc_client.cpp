#include "network/webrtc_client.h"
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

WebRTCClient::WebRTCClient() = default;

WebRTCClient::~WebRTCClient() noexcept {
    Shutdown();
}

bool WebRTCClient::Initialize() {
    Shutdown();

    rtc::Configuration config;
    config.enableIceTcp = false;

    pc_ = std::make_shared<rtc::PeerConnection>(config);

    pc_->onLocalDescription([this](rtc::Description desc) {
        std::cout << "[WebRTC Client] Local Description (" << desc.typeString() << ") -> Signaling" << std::endl;
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

    pc_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
        std::string label = dc->label();
        std::cout << "[WebRTC Client] Inbound DataChannel: " << label << std::endl;
        
        if (label == "video") {
            videoChannel_ = dc;
            videoChannel_->onMessage([this](std::variant<rtc::binary, std::string> data) {
                if (std::holds_alternative<rtc::binary>(data) && videoCallback_) {
                    const auto& bin = std::get<rtc::binary>(data);
                    videoCallback_(reinterpret_cast<const uint8_t*>(bin.data()), bin.size());
                }
            });
        } else if (label == "input") {
            inputChannel_ = dc;
        } else if (label == "audio") {
            audioChannel_ = dc;
            audioChannel_->onMessage([this](std::variant<rtc::binary, std::string> data) {
                if (std::holds_alternative<rtc::binary>(data) && audioCallback_) {
                    const auto& bin = std::get<rtc::binary>(data);
                    audioCallback_(reinterpret_cast<const uint8_t*>(bin.data()), bin.size());
                }
            });
        } else if (label == "cursor") {
            cursorChannel_ = dc;
            cursorChannel_->onMessage([this](std::variant<rtc::binary, std::string> data) {
                if (std::holds_alternative<rtc::binary>(data)) {
                    const auto& bin = std::get<rtc::binary>(data);
                    if (bin.size() >= sizeof(MessageType)) {
                        auto type = *reinterpret_cast<const MessageType*>(bin.data());
                        if (type == MessageType::CursorPosition && bin.size() >= sizeof(CursorPositionMessage) && cursorPositionCallback_) {
                            const auto* pos = reinterpret_cast<const CursorPositionMessage*>(bin.data());
                            cursorPositionCallback_(*pos);
                        } else if (type == MessageType::CursorShape && bin.size() >= sizeof(CursorShapeMessage) && cursorShapeCallback_) {
                            const auto* shape = reinterpret_cast<const CursorShapeMessage*>(bin.data());
                            const uint8_t* shapeData = reinterpret_cast<const uint8_t*>(bin.data()) + sizeof(CursorShapeMessage);
                            cursorShapeCallback_(*shape, shapeData);
                        }
                    }
                }
            });
        } else if (label == "control") {
            controlChannel_ = dc;
            controlChannel_->onOpen([this]() {
                RequestIDR();
            });
        }
    });

    pc_->onStateChange([this](rtc::PeerConnection::State state) {
        std::cout << "[WebRTC Client] State: " << state << std::endl;
        connected_ = (state == rtc::PeerConnection::State::Connected);
        if (connected_) {
            RequestIDR();
        }
    });

    return true;
}

void WebRTCClient::Shutdown() noexcept {
    connected_ = false;
    hasRemoteDescription_ = false;
    pendingCandidates_.clear();

    if (videoChannel_) { videoChannel_->close(); videoChannel_.reset(); }
    if (inputChannel_) { inputChannel_->close(); inputChannel_.reset(); }
    if (audioChannel_) { audioChannel_->close(); audioChannel_.reset(); }
    if (cursorChannel_) { cursorChannel_->close(); cursorChannel_.reset(); }
    if (controlChannel_) { controlChannel_->close(); controlChannel_.reset(); }
    if (pc_) { pc_->close(); pc_.reset(); }
}

void WebRTCClient::ProcessSignalingMessage(const std::string& msg) {
    try {
        auto data = json::parse(msg);
        std::string type = data.value("type", "");

        if (type == "offer") {
            std::cout << "[WebRTC Client] Received Offer -> Setting Remote Description" << std::endl;
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
        std::cerr << "[WebRTC Client] Signaling Error: " << e.what() << std::endl;
    }
}

void WebRTCClient::SendInputData(const uint8_t* data, size_t size) {
    if (inputChannel_ && inputChannel_->isOpen() && data && size > 0) {
        rtc::binary payload(
            reinterpret_cast<const std::byte*>(data),
            reinterpret_cast<const std::byte*>(data + size)
        );
        inputChannel_->send(std::move(payload));
    }
}

void WebRTCClient::RequestIDR() {
    if (controlChannel_ && controlChannel_->isOpen()) {
        ControlCommandMessage msg;
        msg.command = ControlCommandType::RequestIDR;
        rtc::binary payload(
            reinterpret_cast<const std::byte*>(&msg),
            reinterpret_cast<const std::byte*>(&msg) + sizeof(ControlCommandMessage)
        );
        controlChannel_->send(std::move(payload));
    }
}