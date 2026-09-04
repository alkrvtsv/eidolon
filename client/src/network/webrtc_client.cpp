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

    pc_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
        std::string label = dc->label();
        std::cout << "[WebRTC Client] Inbound DataChannel: " << label << std::endl;
        
        if (label == "video") {
            videoChannel_ = dc;
            videoChannel_->onMessage([this](std::variant<rtc::binary, std::string> data) {
                if (std::holds_alternative<rtc::binary>(data)) {
                    const auto& bin = std::get<rtc::binary>(data);
                    ProcessVideoChunk(reinterpret_cast<const uint8_t*>(bin.data()), bin.size());
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

bool WebRTCClient::IsKeyframe(const uint8_t* data, size_t size) {
    if (!data || size < 5) return false;
    for (size_t i = 0; i + 4 < size; ++i) {
        if (data[i] == 0 && data[i + 1] == 0) {
            size_t nalStart = 0;
            if (data[i + 2] == 1) {
                nalStart = i + 3;
            } else if (data[i + 2] == 0 && data[i + 3] == 1) {
                nalStart = i + 4;
            }
            if (nalStart > 0 && nalStart < size) {
                uint8_t nalType = data[nalStart] & 0x1F;
                if (nalType == 5 || nalType == 7) {
                    return true;
                }
            }
        }
    }
    return false;
}

void WebRTCClient::ProcessVideoChunk(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(VideoChunkHeader)) return;

    const auto* hdr = reinterpret_cast<const VideoChunkHeader*>(data);
    const uint8_t* payload = data + sizeof(VideoChunkHeader);
    const size_t payloadSize = size - sizeof(VideoChunkHeader);

    int32_t diff = static_cast<int32_t>(hdr->frameId - activeFrameId_);

    if (diff > 0) {
        if (activeFrameId_ != 0 && (!frameCompleted_ || diff > 1)) {
            waitingForIDR_ = true;
            RequestIDR();
        }

        activeFrameId_ = hdr->frameId;
        expectedFrameSize_ = hdr->frameSize;
        totalChunks_ = hdr->totalChunks;
        receivedChunksCount_ = 0;
        frameCompleted_ = false;

        if (frameBuffer_.size() < expectedFrameSize_) {
            frameBuffer_.resize(expectedFrameSize_);
        }
        receivedChunksMask_.assign(totalChunks_, false);
    } else if (diff < 0) {
        return;
    }

    if (hdr->chunkIndex >= totalChunks_ || frameCompleted_) {
        return;
    }

    if (!receivedChunksMask_[hdr->chunkIndex]) {
        const size_t kMaxPayload = 64 * 1024;
        size_t offset = static_cast<size_t>(hdr->chunkIndex) * kMaxPayload;

        if (offset + payloadSize <= expectedFrameSize_) {
            std::memcpy(frameBuffer_.data() + offset, payload, payloadSize);
            receivedChunksMask_[hdr->chunkIndex] = true;
            receivedChunksCount_++;

            if (receivedChunksCount_ == totalChunks_) {
                frameCompleted_ = true;

                if (waitingForIDR_) {
                    if (IsKeyframe(frameBuffer_.data(), expectedFrameSize_)) {
                        waitingForIDR_ = false;
                    } else {
                        return;
                    }
                }

                if (videoCallback_) {
                    videoCallback_(frameBuffer_.data(), expectedFrameSize_);
                }
            }
        }
    }
}

void WebRTCClient::Shutdown() noexcept {
    connected_ = false;
    hasRemoteDescription_ = false;
    pendingCandidates_.clear();

    activeFrameId_ = 0;
    expectedFrameSize_ = 0;
    totalChunks_ = 0;
    receivedChunksCount_ = 0;
    frameCompleted_ = false;
    waitingForIDR_ = true;
    frameBuffer_.clear();
    receivedChunksMask_.clear();

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