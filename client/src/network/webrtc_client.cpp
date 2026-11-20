#include "network/webrtc_client.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <iostream>

using json = nlohmann::json;

WebRTCClient::WebRTCClient() {
    lastFeedbackTime_ = std::chrono::steady_clock::now();
    lastBitrateCalcTime_ = std::chrono::steady_clock::now();
}

WebRTCClient::~WebRTCClient() noexcept {
    Shutdown();
}

bool WebRTCClient::Initialize() {
    Shutdown();
    RecreatePeerConnection();
    return true;
}

void WebRTCClient::RecreatePeerConnection() {
    if (videoTrack_) { videoTrack_->close(); videoTrack_.reset(); }
    if (inputChannel_) { inputChannel_->close(); inputChannel_.reset(); }
    if (audioChannel_) { audioChannel_->close(); audioChannel_.reset(); }
    if (cursorChannel_) { cursorChannel_->close(); cursorChannel_.reset(); }
    if (controlChannel_) { controlChannel_->close(); controlChannel_.reset(); }
    if (pc_) { pc_->close(); pc_.reset(); }

    connected_ = false;
    hasRemoteDescription_ = false;
    restartRequested_ = false;
    peerConnectionGeneration_++;
    pendingCandidates_.clear();
    reorderBuffer_.clear();
    hasExpectedSeq_ = false;
    nextExpectedSeq_ = 0;
    assembledFrameBuffer_.clear();
    fuBuffer_.clear();
    hasFrameData_ = false;
    receivedSpsPps_ = false;
    isFrameCorrupted_ = false;
    currentFrameTimestamp_ = 0;

    rtc::Configuration config;
    config.enableIceTcp = false;

    pc_ = std::make_shared<rtc::PeerConnection>(config);

    uint64_t myGeneration = peerConnectionGeneration_;

    pc_->onLocalDescription([this, myGeneration](rtc::Description desc) {
        if (myGeneration != peerConnectionGeneration_) return;
        std::cout << "[WebRTC Client] Local Description (" << desc.typeString() << ") -> Signaling" << std::endl;
        if (signalingSend_) {
            json msg = { {"type", desc.typeString()}, {"sdp", std::string(desc)} };
            signalingSend_(msg.dump());
        }
    });

    pc_->onLocalCandidate([this, myGeneration](rtc::Candidate cand) {
        if (myGeneration != peerConnectionGeneration_) return;
        if (signalingSend_) {
            json msg = { {"type", "ice_candidate"}, {"candidate", cand.candidate()}, {"sdpMid", cand.mid()} };
            signalingSend_(msg.dump());
        }
    });

    pc_->onTrack([this, myGeneration](std::shared_ptr<rtc::Track> track) {
        if (myGeneration != peerConnectionGeneration_) return;
        std::cout << "[WebRTC Client] Inbound Track: " << track->description().type() << std::endl;
        videoTrack_ = track;
        videoTrack_->onMessage([this, myGeneration](std::variant<rtc::binary, std::string> data) {
            if (myGeneration != peerConnectionGeneration_) return;
            if (std::holds_alternative<rtc::binary>(data)) {
                const auto& bin = std::get<rtc::binary>(data);
                OnRtpPacketReceived(reinterpret_cast<const uint8_t*>(bin.data()), bin.size());
            }
        });
    });

    pc_->onDataChannel([this, myGeneration](std::shared_ptr<rtc::DataChannel> dc) {
        if (myGeneration != peerConnectionGeneration_) return;
        std::string label = dc->label();
        std::cout << "[WebRTC Client] Inbound DataChannel: " << label << std::endl;

        if (label == "input") {
            inputChannel_ = dc;
        } else if (label == "audio") {
            audioChannel_ = dc;
            audioChannel_->onMessage([this, myGeneration](std::variant<rtc::binary, std::string> data) {
                if (myGeneration != peerConnectionGeneration_) return;
                if (std::holds_alternative<rtc::binary>(data) && audioCallback_) {
                    const auto& bin = std::get<rtc::binary>(data);
                    audioCallback_(reinterpret_cast<const uint8_t*>(bin.data()), bin.size());
                }
            });
        } else if (label == "cursor") {
            cursorChannel_ = dc;
            cursorChannel_->onMessage([this, myGeneration](std::variant<rtc::binary, std::string> data) {
                if (myGeneration != peerConnectionGeneration_) return;
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
            controlChannel_->onOpen([this, myGeneration]() {
                if (myGeneration != peerConnectionGeneration_) return;
                if (hasPendingConfig_) {
                    SendClientConfig(pendingConfig_);
                }
                RequestIDR();
            });
        }
    });

    pc_->onStateChange([this, myGeneration](rtc::PeerConnection::State state) {
        if (myGeneration != peerConnectionGeneration_) return;
        std::cout << "[WebRTC Client] State: " << state << std::endl;
        connected_ = (state == rtc::PeerConnection::State::Connected);

        if (connected_) {
            restartRequested_ = false;
            if (hasPendingConfig_) {
                SendClientConfig(pendingConfig_);
            }
            RequestIDR();
        } else if (state == rtc::PeerConnection::State::Failed) {
            connected_ = false;
            if (!restartRequested_) {
                restartRequested_ = true;
                std::cout << "[WebRTC Client] Connection failed -> requesting restart" << std::endl;
                if (signalingSend_) {
                    json restartMsg = { {"type", "restart"} };
                    signalingSend_(restartMsg.dump());
                }
            }
        }
    });
}

void WebRTCClient::CheckAndSendFeedback() {
    auto now = std::chrono::steady_clock::now();
    auto elapsedFeedbackMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFeedbackTime_).count();

    if (elapsedFeedbackMs >= 200) {
        uint32_t totalPackets = periodReceivedPackets_ + periodLostPackets_;
        if (totalPackets > 0) {
            recentLossRatePct_.store(
                (static_cast<float>(periodLostPackets_) * 100.0f) / static_cast<float>(totalPackets),
                std::memory_order_relaxed
            );
        } else {
            recentLossRatePct_.store(0.0f, std::memory_order_relaxed);
        }

        if (controlChannel_ && controlChannel_->isOpen()) {
            NetworkFeedbackMessage msg;
            msg.type = MessageType::NetworkFeedback;
            msg.receivedPackets = periodReceivedPackets_;
            msg.lostPackets = periodLostPackets_;
            msg.frameJitterMs = 0;

            rtc::binary payload(
                reinterpret_cast<const std::byte*>(&msg),
                reinterpret_cast<const std::byte*>(&msg) + sizeof(NetworkFeedbackMessage)
            );
            controlChannel_->send(std::move(payload));
        }

        periodReceivedPackets_ = 0;
        periodLostPackets_ = 0;
        lastFeedbackTime_ = now;
    }

    auto elapsedBitrateMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBitrateCalcTime_).count();
    if (elapsedBitrateMs >= 200) {
        float instantMbps = (static_cast<float>(periodByteCount_ * 8)) / (static_cast<float>(elapsedBitrateMs) * 1000.0f);
        float currentSmoothed = incomingBitrateMbps_.load(std::memory_order_relaxed);

        if (currentSmoothed <= 0.01f) {
            incomingBitrateMbps_.store(instantMbps, std::memory_order_relaxed);
        } else {
            float smoothed = (currentSmoothed * 0.7f) + (instantMbps * 0.3f);
            incomingBitrateMbps_.store(smoothed, std::memory_order_relaxed);
        }

        periodByteCount_ = 0;
        lastBitrateCalcTime_ = now;
    }
}

void WebRTCClient::OnRtpPacketReceived(const uint8_t* data, size_t size) {
    if (!data || size < 12) return;

    periodReceivedPackets_++;
    periodByteCount_ += size;

    CheckAndSendFeedback();

    uint16_t seq = (static_cast<uint16_t>(data[2]) << 8) | static_cast<uint16_t>(data[3]);

    if (!hasExpectedSeq_) {
        nextExpectedSeq_ = seq;
        hasExpectedSeq_ = true;
    }

    if (SequenceLessThan(seq, nextExpectedSeq_)) {
        return;
    }

    reorderBuffer_[seq].assign(data, data + size);
    DrainReorderBuffer();
}

void WebRTCClient::DrainReorderBuffer() {
    auto now = std::chrono::steady_clock::now();

    while (!reorderBuffer_.empty()) {
        auto it = reorderBuffer_.begin();
        uint16_t seq = it->first;

        if (seq == nextExpectedSeq_) {
            ProcessOrderedPacket(it->second.data(), it->second.size());
            nextExpectedSeq_ = static_cast<uint16_t>(nextExpectedSeq_ + 1);
            reorderBuffer_.erase(it);
            hasGapDetected_ = false;
        } else {
            bool forceFlush = false;

            if (reorderBuffer_.size() >= 16) {
                forceFlush = true;
            } else if (hasGapDetected_) {
                auto gapElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - gapDetectedTime_).count();
                if (gapElapsed >= kReorderTimeoutMs) {
                    forceFlush = true;
                }
            }

            if (forceFlush) {
                uint16_t lost = static_cast<uint16_t>(seq - nextExpectedSeq_);
                lostPacketCount_ += lost;
                periodLostPackets_ += lost;
                isFrameCorrupted_ = true;
                waitingForIDR_ = true;
                waitingForIDRSince_ = now;
                receivedSpsPps_ = false;
                fuBuffer_.clear();
                hasGapDetected_ = false;

                std::cerr << "[WebRTC RTP DROP] Lost " << lost << " packets! Waiting for IDR. Expected seq: "
                          << nextExpectedSeq_ << ", advancing to: " << seq
                          << " (Total lost: " << lostPacketCount_ << ")" << std::endl;

                auto idrElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastIdrRequestTime_).count();
                if (idrElapsed >= 150) {
                    lastIdrRequestTime_ = now;
                    RequestIDR();
                }

                ProcessOrderedPacket(it->second.data(), it->second.size());
                nextExpectedSeq_ = static_cast<uint16_t>(seq + 1);
                reorderBuffer_.erase(it);
            } else {
                if (!hasGapDetected_) {
                    hasGapDetected_ = true;
                    gapDetectedTime_ = now;
                }
                break;
            }
        }
    }
}

void WebRTCClient::DispatchAssembledFrame() {
    if (!assembledFrameBuffer_.empty() && !isFrameCorrupted_) {
        if (waitingForIDR_) {
            if (ContainsKeyframeNal()) {
                waitingForIDR_ = false;
                std::cout << "[WebRTC] IDR received, resuming decode" << std::endl;
            } else {
                auto now = std::chrono::steady_clock::now();
                auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - waitingForIDRSince_).count();
                if (waitMs >= kIdrWaitSafetyMs) {
                    waitingForIDRSince_ = now;
                    RequestIDR();
                }
                assembledFrameBuffer_.clear();
                fuBuffer_.clear();
                hasFrameData_ = false;
                return;
            }
        }

        if (!receivedSpsPps_) {
            const uint8_t* buf = assembledFrameBuffer_.data();
            size_t bufSize = assembledFrameBuffer_.size();
            for (size_t i = 0; i + 4 < bufSize; ++i) {
                if (buf[i] == 0 && buf[i + 1] == 0 && ((buf[i + 2] == 1) || (buf[i + 2] == 0 && buf[i + 3] == 1))) {
                    size_t typeIdx = (buf[i + 2] == 1) ? (i + 3) : (i + 4);
                    if (typeIdx < bufSize) {
                        uint8_t nType = (buf[typeIdx] >> 1) & 0x3F;
                        if (nType == 32 || nType == 33 || nType == 34) {
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

void WebRTCClient::ProcessOrderedPacket(const uint8_t* data, size_t size) {
    if (!data || size < 12) return;

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

    uint8_t nalType = (payload[0] >> 1) & 0x3F;

    if (nalType == 49) {
        if (payloadSize < 3) return;
        uint8_t payloadHdr0 = payload[0];
        uint8_t payloadHdr1 = payload[1];
        uint8_t fuHeader = payload[2];
        bool isStart = (fuHeader & 0x80) != 0;
        bool isEnd = (fuHeader & 0x40) != 0;
        uint8_t originalNalType = fuHeader & 0x3F;

        if (isStart) {
            fuBuffer_.clear();
            fuBuffer_.push_back(0);
            fuBuffer_.push_back(0);
            fuBuffer_.push_back(0);
            fuBuffer_.push_back(1);
            uint8_t reconstructedHdr0 = (payloadHdr0 & 0x81) | (originalNalType << 1);
            uint8_t reconstructedHdr1 = payloadHdr1;
            fuBuffer_.push_back(reconstructedHdr0);
            fuBuffer_.push_back(reconstructedHdr1);
        }

        if (!fuBuffer_.empty()) {
            fuBuffer_.insert(fuBuffer_.end(), payload + 3, payload + payloadSize);
            if (isEnd) {
                assembledFrameBuffer_.insert(assembledFrameBuffer_.end(), fuBuffer_.begin(), fuBuffer_.end());
                fuBuffer_.clear();
            }
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

bool WebRTCClient::ContainsKeyframeNal() const {
    const uint8_t* buf = assembledFrameBuffer_.data();
    size_t bufSize = assembledFrameBuffer_.size();
    for (size_t i = 0; i + 4 < bufSize; ++i) {
        if (buf[i] == 0 && buf[i + 1] == 0 && ((buf[i + 2] == 1) || (buf[i + 2] == 0 && buf[i + 3] == 1))) {
            size_t typeIdx = (buf[i + 2] == 1) ? (i + 3) : (i + 4);
            if (typeIdx < bufSize) {
                uint8_t nType = (buf[typeIdx] >> 1) & 0x3F;
                if (nType == 19 || nType == 20 || nType == 21 || nType == 32 || nType == 33 || nType == 34) {
                    return true;
                }
            }
        }
    }
    return false;
}

void WebRTCClient::Shutdown() noexcept {
    connected_ = false;
    hasRemoteDescription_ = false;
    pendingCandidates_.clear();

    reorderBuffer_.clear();
    hasExpectedSeq_ = false;
    nextExpectedSeq_ = 0;

    assembledFrameBuffer_.clear();
    fuBuffer_.clear();
    hasFrameData_ = false;
    receivedSpsPps_ = false;
    isFrameCorrupted_ = false;
    waitingForIDR_ = false;
    hasGapDetected_ = false;
    currentFrameTimestamp_ = 0;
    lostPacketCount_ = 0;
    periodReceivedPackets_ = 0;
    periodLostPackets_ = 0;
    periodByteCount_ = 0;
    incomingBitrateMbps_.store(0.0f, std::memory_order_relaxed);
    recentLossRatePct_.store(0.0f, std::memory_order_relaxed);

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
            if (hasRemoteDescription_) {
                std::cout << "[WebRTC Client] Recreating PC for new session" << std::endl;
                RecreatePeerConnection();
            }
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