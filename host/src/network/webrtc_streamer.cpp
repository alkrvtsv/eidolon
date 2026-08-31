#include "network/webrtc_streamer.h"
#include <rtc/h264rtppacketizer.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

WebRTCStreamer::WebRTCStreamer() {
    cursorPayloadBuffer_.reserve(64 * 64 * 4 + sizeof(CursorShapeMessage));
}

WebRTCStreamer::~WebRTCStreamer() noexcept {
    Shutdown();
}

bool WebRTCStreamer::Initialize() {
    Shutdown();

    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    config.enableIceTcp = false;

    pc_ = std::make_shared<rtc::PeerConnection>(config);

    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96);
    media.addSSRC(7777, "video-stream");

    videoTrack_ = pc_->addTrack(media);

    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(7777, "video", 96, 90000);
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::H264RtpPacketizer::Separator::LongStartSequence,
        rtpConfig
    );
    videoTrack_->setMediaHandler(packetizer);

    SetupDataChannels();

    pc_->onStateChange([this](rtc::PeerConnection::State state) {
        peerConnected_ = (state == rtc::PeerConnection::State::Connected);
    });

    pc_->onLocalDescription([this](rtc::Description description) {
        if (signalingSend_) {
            json msg = {
                {"type", description.typeString()},
                {"sdp", std::string(description)}
            };
            signalingSend_(msg.dump());
        }
    });

    pc_->onLocalCandidate([this](rtc::Candidate candidate) {
        if (signalingSend_) {
            json msg = {
                {"type", "ice_candidate"},
                {"candidate", candidate.candidate()},
                {"sdpMid", candidate.mid()}
            };
            signalingSend_(msg.dump());
        }
    });

    return true;
}

void WebRTCStreamer::SetupDataChannels() {
    rtc::DataChannelInit inputInit;
    inputInit.reliability.unordered = true;
    inputInit.reliability.maxPacketLifeTime = std::chrono::milliseconds(0);
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

void WebRTCStreamer::Shutdown() noexcept {
    peerConnected_ = false;

    if (inputChannel_) {
        inputChannel_->close();
        inputChannel_.reset();
    }
    if (audioChannel_) {
        audioChannel_->close();
        audioChannel_.reset();
    }
    if (cursorChannel_) {
        cursorChannel_->close();
        cursorChannel_.reset();
    }
    if (controlChannel_) {
        controlChannel_->close();
        controlChannel_.reset();
    }
    if (videoTrack_) {
        videoTrack_->close();
        videoTrack_.reset();
    }
    if (pc_) {
        pc_->close();
        pc_.reset();
    }
}

void WebRTCStreamer::ProcessSignalingMessage(const std::string& msg) {
    try {
        auto data = json::parse(msg);
        std::string type = data.value("type", "");

        if (type == "offer") {
            pc_->setRemoteDescription(rtc::Description(data["sdp"], type));
            pc_->setLocalDescription();
        } else if (type == "answer") {
            pc_->setRemoteDescription(rtc::Description(data["sdp"], type));
        } else if (type == "ice_candidate") {
            pc_->addRemoteCandidate(rtc::Candidate(data["candidate"], data["sdpMid"]));
        }
    } catch (...) {}
}

void WebRTCStreamer::SendVideoFrame(const uint8_t* data, size_t size) {
    if (videoTrack_ && videoTrack_->isOpen() && data && size > 0) {
        rtc::binary frame(reinterpret_cast<const std::byte*>(data), reinterpret_cast<const std::byte*>(data + size));
        videoTrack_->send(std::move(frame));
    }
}

void WebRTCStreamer::SendAudioFrame(const uint8_t* data, size_t size) {
    if (audioChannel_ && audioChannel_->isOpen() && data && size > 0) {
        rtc::binary frame(reinterpret_cast<const std::byte*>(data), reinterpret_cast<const std::byte*>(data + size));
        audioChannel_->send(std::move(frame));
    }
}

void WebRTCStreamer::SendCursorShape(const CursorShapeMessage& shape, const uint8_t* data) {
    if (!cursorChannel_ || !cursorChannel_->isOpen()) {
        return;
    }

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
    if (cursorChannel_ && cursorChannel_->isOpen()) {
        rtc::binary payload(
            reinterpret_cast<const std::byte*>(&pos),
            reinterpret_cast<const std::byte*>(&pos) + sizeof(CursorPositionMessage)
        );
        cursorChannel_->send(std::move(payload));
    }
}