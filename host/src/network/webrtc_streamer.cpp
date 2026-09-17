#include "network/webrtc_streamer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>

using json = nlohmann::json;

WebRTCStreamer::WebRTCStreamer() {
    cursorPayloadBuffer_.reserve(64 * 64 * 4 + sizeof(CursorShapeMessage));
    rtpPacketBuffer_.resize(1500);
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

    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96);
    media.addSSRC(rtpSsrc_, "video-stream");
    videoTrack_ = pc_->addTrack(media);

    SetupDataChannels();
}

void WebRTCStreamer::SetupDataChannels() {
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
        if (std::holds_alternative<rtc::binary>(data)) {
            const auto& bin = std::get<rtc::binary>(data);
            if (bin.size() >= sizeof(MessageType)) {
                auto type = *reinterpret_cast<const MessageType*>(bin.data());
                if (type == MessageType::ControlCommand && bin.size() >= sizeof(ControlCommandMessage)) {
                    const auto* cmd = reinterpret_cast<const ControlCommandMessage*>(bin.data());
                    if (controlCallback_) {
                        controlCallback_(cmd->command);
                    }
                } else if (type == MessageType::ClientConfig && bin.size() >= sizeof(ClientConfigMessage)) {
                    const auto* cfg = reinterpret_cast<const ClientConfigMessage*>(bin.data());
                    if (clientConfigCallback_) {
                        clientConfigCallback_(*cfg);
                    }
                }
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

    if (videoTrack_) { videoTrack_->close(); videoTrack_.reset(); }
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

void WebRTCStreamer::SendRtpPacket(const uint8_t* payload, size_t payloadSize, bool marker, uint32_t rtpTimestamp) {
    if (!videoTrack_ || !videoTrack_->isOpen()) return;

    size_t packetSize = 12 + payloadSize;
    if (rtpPacketBuffer_.size() < packetSize) {
        rtpPacketBuffer_.resize(packetSize);
    }

    uint8_t* rtp = rtpPacketBuffer_.data();
    rtp[0] = 0x80;
    rtp[1] = static_cast<uint8_t>((marker ? 0x80 : 0x00) | (96 & 0x7F));
    rtp[2] = static_cast<uint8_t>((rtpSequenceNumber_ >> 8) & 0xFF);
    rtp[3] = static_cast<uint8_t>(rtpSequenceNumber_ & 0xFF);
    rtpSequenceNumber_++;

    rtp[4] = static_cast<uint8_t>((rtpTimestamp >> 24) & 0xFF);
    rtp[5] = static_cast<uint8_t>((rtpTimestamp >> 16) & 0xFF);
    rtp[6] = static_cast<uint8_t>((rtpTimestamp >> 8) & 0xFF);
    rtp[7] = static_cast<uint8_t>(rtpTimestamp & 0xFF);

    rtp[8] = static_cast<uint8_t>((rtpSsrc_ >> 24) & 0xFF);
    rtp[9] = static_cast<uint8_t>((rtpSsrc_ >> 16) & 0xFF);
    rtp[10] = static_cast<uint8_t>((rtpSsrc_ >> 8) & 0xFF);
    rtp[11] = static_cast<uint8_t>(rtpSsrc_ & 0xFF);

    std::memcpy(rtp + 12, payload, payloadSize);

    rtc::binary bin(reinterpret_cast<const std::byte*>(rtp), reinterpret_cast<const std::byte*>(rtp + packetSize));
    videoTrack_->send(std::move(bin));
}

bool WebRTCStreamer::SendVideoFrame(const uint8_t* data, size_t size, uint64_t captureTimestampUs) {
    if (!peerConnected_ || !videoTrack_ || !videoTrack_->isOpen() || !data || size == 0) {
        return false;
    }

    try {
        uint32_t rtpTimestamp = static_cast<uint32_t>((captureTimestampUs * 9) / 100);

        std::vector<std::pair<size_t, size_t>> nals;
        size_t i = 0;
        while (i < size) {
            if (i + 2 < size && data[i] == 0 && data[i + 1] == 0) {
                size_t startCodeLen = 0;
                if (data[i + 2] == 1) {
                    startCodeLen = 3;
                } else if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1) {
                    startCodeLen = 4;
                }

                if (startCodeLen > 0) {
                    size_t nalStart = i + startCodeLen;
                    if (!nals.empty()) {
                        nals.back().second = i - nals.back().first;
                    }
                    nals.emplace_back(nalStart, 0);
                    i = nalStart;
                    continue;
                }
            }
            i++;
        }

        if (!nals.empty()) {
            nals.back().second = size - nals.back().first;
        }

        constexpr size_t kMaxRtpPayload = 1180;

        for (size_t nalIdx = 0; nalIdx < nals.size(); ++nalIdx) {
            const auto& [nalOffset, nalSize] = nals[nalIdx];
            if (nalSize == 0) continue;

            const uint8_t* nalData = data + nalOffset;
            bool isLastNal = (nalIdx == nals.size() - 1);

            if (nalSize <= kMaxRtpPayload) {
                SendRtpPacket(nalData, nalSize, isLastNal, rtpTimestamp);
            } else {
                uint8_t nalHeader = nalData[0];
                uint8_t fnri = nalHeader & 0xE0;
                uint8_t nalType = nalHeader & 0x1F;

                const uint8_t* payloadData = nalData + 1;
                size_t payloadRemaining = nalSize - 1;
                bool isStart = true;

                while (payloadRemaining > 0) {
                    size_t chunkPayloadSize = (std::min)(kMaxRtpPayload - 2, payloadRemaining);
                    bool isEnd = (chunkPayloadSize == payloadRemaining);

                    uint8_t fuIndicator = fnri | 28;
                    uint8_t fuHeader = (isStart ? 0x80 : 0x00) | (isEnd ? 0x40 : 0x00) | nalType;

                    std::vector<uint8_t> fuPacket(2 + chunkPayloadSize);
                    fuPacket[0] = fuIndicator;
                    fuPacket[1] = fuHeader;
                    std::memcpy(fuPacket.data() + 2, payloadData, chunkPayloadSize);

                    bool marker = isLastNal && isEnd;
                    SendRtpPacket(fuPacket.data(), fuPacket.size(), marker, rtpTimestamp);

                    payloadData += chunkPayloadSize;
                    payloadRemaining -= chunkPayloadSize;
                    isStart = false;
                }
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[WebRTC Host WARNING] SendVideoFrame: " << e.what() << std::endl;
        return false;
    } catch (...) {
        return false;
    }
}

void WebRTCStreamer::SendAudioFrame(const uint8_t* data, size_t size) {
    if (!peerConnected_ || !audioChannel_ || !audioChannel_->isOpen() || !data || size == 0) return;

    try {
        rtc::binary frame(reinterpret_cast<const std::byte*>(data), reinterpret_cast<const std::byte*>(data + size));
        audioChannel_->send(std::move(frame));
    } catch (...) {}
}

void WebRTCStreamer::SendCursorShape(const CursorShapeMessage& shape, const uint8_t* data) {
    if (!peerConnected_ || !cursorChannel_ || !cursorChannel_->isOpen()) return;

    try {
        size_t totalSize = sizeof(CursorShapeMessage) + shape.dataSize;
        if (totalSize > 128 * 1024) return;

        if (cursorPayloadBuffer_.size() < totalSize) {
            cursorPayloadBuffer_.resize(totalSize);
        }

        std::memcpy(cursorPayloadBuffer_.data(), &shape, sizeof(CursorShapeMessage));
        if (data && shape.dataSize > 0) {
            std::memcpy(cursorPayloadBuffer_.data() + sizeof(CursorShapeMessage), data, shape.dataSize);
        }

        rtc::binary payload(
            reinterpret_cast<const std::byte*>(cursorPayloadBuffer_.data()),
            reinterpret_cast<const std::byte*>(cursorPayloadBuffer_.data() + totalSize)
        );
        cursorChannel_->send(std::move(payload));
    } catch (...) {}
}

void WebRTCStreamer::SendCursorPosition(const CursorPositionMessage& pos) {
    if (!peerConnected_ || !cursorChannel_ || !cursorChannel_->isOpen()) return;

    try {
        rtc::binary payload(
            reinterpret_cast<const std::byte*>(&pos),
            reinterpret_cast<const std::byte*>(&pos) + sizeof(CursorPositionMessage)
        );
        cursorChannel_->send(std::move(payload));
    } catch (...) {}
}