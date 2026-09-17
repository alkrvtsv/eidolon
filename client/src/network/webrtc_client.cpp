#include "network/webrtc_client.h"
#include <nlohmann/json.hpp>
#include <cstring>
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

    pc_->onTrack([this](std::shared_ptr<rtc::Track> track) {
        std::cout << "[WebRTC Client] Inbound Track: " << track->description().type() << std::endl;
        videoTrack_ = track;
        videoTrack_->onMessage([this](std::variant<rtc::binary, std::string> data) {
            if (std::holds_alternative<rtc::binary>(data)) {
                const auto& bin = std::get<rtc::binary>(data);
                ProcessRtpPacket(reinterpret_cast<const uint8_t*>(bin.data()), bin.size());
            }
        });
    });

    pc_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
        std::string label = dc->label();
        std::cout << "[WebRTC Client] Inbound DataChannel: " << label << std::endl;

        if (label == "input") {
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
                if (hasPendingConfig_) {
                    SendClientConfig(pendingConfig_);
                }
                RequestIDR();
            });
        }
    });

    pc_->onStateChange([this](rtc::PeerConnection::State state) {
        std::cout << "[WebRTC Client] State: " << state << std::endl;
        connected_ = (state == rtc::PeerConnection::State::Connected);
        if (connected_) {
            if (hasPendingConfig_) {
                SendClientConfig(pendingConfig_);
            }
            RequestIDR();
        }
    });

    return true;
}

void WebRTCClient::DispatchAssembledFrame() {
    if (!assembledFrameBuffer_.empty() && !isFrameCorrupted_) {
        if (!receivedSpsPps_) {
            const uint8_t* buf = assembledFrameBuffer_.data();
            size_t bufSize = assembledFrameBuffer_.size();
            for (size_t i = 0; i + 4 < bufSize; ++i) {
                if (buf[i] == 0 && buf[i + 1] == 0 && ((buf[i + 2] == 1) || (buf[i + 2] == 0 && buf[i + 3] == 1))) {
                    size_t typeIdx = (buf[i + 2] == 1) ? (i + 3) : (i + 4);
                    if (typeIdx < bufSize) {
                        uint8_t nType = buf[typeIdx] & 0x1F;
                        if (nType == 7) {
                            receivedSpsPps_ = true;
                            break;
                        }
                    }
                }
            }
        }

        if (receivedSpsPps_ && videoCallback_) {
            uint64_t captureTsUs = (static_cast<uint64_t>(currentFrameTimestamp_) * 100) / 9;
            videoCallback_(assembledFrameBuffer_.data(), assembledFrameBuffer_.size(), captureTsUs);
        }
    }

    assembledFrameBuffer_.clear();
    fuBuffer_.clear();
    hasFrameData_ = false;
    isFrameCorrupted_ = false;
}

void WebRTCClient::ProcessRtpPacket(const uint8_t* data, size_t size) {
    if (!data || size < 12) return;

    uint16_t seq = (static_cast<uint16_t>(data[2]) << 8) | static_cast<uint16_t>(data[3]);
    if (hasLastSequenceNumber_) {
        uint16_t diff = seq - lastSequenceNumber_;
        if (diff > 1 && diff < 30000) {
            uint16_t lost = diff - 1;
            lostPacketCount_ += lost;
            isFrameCorrupted_ = true;
            fuBuffer_.clear();
            std::cerr << "[WebRTC RTP DROP] Lost " << lost << " packets! Frame corrupted. Expected seq: "
                      << static_cast<uint16_t>(lastSequenceNumber_ + 1)
                      << ", got: " << seq
                      << " (Total lost: " << lostPacketCount_ << ")" << std::endl;
        }
    }
    lastSequenceNumber_ = seq;
    hasLastSequenceNumber_ = true;

    bool marker = (data[1] & 0x80) != 0;
    uint32_t rtpTimestamp = (static_cast<uint32_t>(data[4]) << 24) |
                            (static_cast<uint32_t>(data[5]) << 16) |
                            (static_cast<uint32_t>(data[6]) << 8) |
                            static_cast<uint32_t>(data[7]);

    const uint8_t* payload = data + 12;
    size_t payloadSize = size - 12;
    if (payloadSize == 0) return;

    if (hasFrameData_ && rtpTimestamp != currentFrameTimestamp_) {
        DispatchAssembledFrame();
    }

    currentFrameTimestamp_ = rtpTimestamp;
    hasFrameData_ = true;

    if (isFrameCorrupted_) {
        if (marker) {
            DispatchAssembledFrame();
        }
        return;
    }

    uint8_t nalType = payload[0] & 0x1F;

    if (nalType == 28) {
        if (payloadSize < 2) return;
        uint8_t fuIndicator = payload[0];
        uint8_t fuHeader = payload[1];
        bool isStart = (fuHeader & 0x80) != 0;
        bool isEnd = (fuHeader & 0x40) != 0;
        uint8_t reconstructedType = (fuIndicator & 0xE0) | (fuHeader & 0x1F);

        if (isStart) {
            fuBuffer_.clear();
            fuBuffer_.push_back(0);
            fuBuffer_.push_back(0);
            fuBuffer_.push_back(0);
            fuBuffer_.push_back(1);
            fuBuffer_.push_back(reconstructedType);
            fuBuffer_.insert(fuBuffer_.end(), payload + 2, payload + payloadSize);
        } else if (!fuBuffer_.empty()) {
            fuBuffer_.insert(fuBuffer_.end(), payload + 2, payload + payloadSize);
        }

        if (isEnd && !fuBuffer_.empty()) {
            assembledFrameBuffer_.insert(assembledFrameBuffer_.end(), fuBuffer_.begin(), fuBuffer_.end());
            fuBuffer_.clear();
        }
    } else {
        assembledFrameBuffer_.push_back(0);
        assembledFrameBuffer_.push_back(0);
        assembledFrameBuffer_.push_back(0);
        assembledFrameBuffer_.push_back(1);
        assembledFrameBuffer_.insert(assembledFrameBuffer_.end(), payload, payload + payloadSize);
    }

    if (marker) {
        DispatchAssembledFrame();
    }
}

void WebRTCClient::Shutdown() noexcept {
    connected_ = false;
    hasRemoteDescription_ = false;
    pendingCandidates_.clear();

    assembledFrameBuffer_.clear();
    fuBuffer_.clear();
    hasFrameData_ = false;
    receivedSpsPps_ = false;
    isFrameCorrupted_ = false;
    currentFrameTimestamp_ = 0;
    hasLastSequenceNumber_ = false;
    lastSequenceNumber_ = 0;
    lostPacketCount_ = 0;

    if (videoTrack_) { videoTrack_->close(); videoTrack_.reset(); }
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

void WebRTCClient::SendClientConfig(const ClientConfigMessage& config) {
    pendingConfig_ = config;
    hasPendingConfig_ = true;

    if (controlChannel_ && controlChannel_->isOpen()) {
        rtc::binary payload(
            reinterpret_cast<const std::byte*>(&config),
            reinterpret_cast<const std::byte*>(&config) + sizeof(ClientConfigMessage)
        );
        controlChannel_->send(std::move(payload));
        std::cout << "[WebRTC Client] Sent ClientConfig: "
                  << config.width << "x" << config.height
                  << " @" << config.refreshRate << "Hz, Max Bitrate: "
                  << config.maxBitrateKbps << " kbps" << std::endl;
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