#pragma once

#include "protocol.h"
#include <rtc/rtc.hpp>
#include <memory>
#include <functional>
#include <atomic>
#include <vector>
#include <string>
#include <utility>
#include <chrono>
#include <deque>
#include <mutex>

class WebRTCStreamer {
public:
    WebRTCStreamer();
    ~WebRTCStreamer() noexcept;

    bool Initialize();
    void Shutdown() noexcept;

    void StartSession();
    void ProcessSignalingMessage(const std::string& msg);
    
    bool SendVideoFrame(const uint8_t* data, size_t size, uint64_t captureTimestampUs = 0);
    void SendAudioFrame(const uint8_t* data, size_t size);
    void SendCursorShape(const CursorShapeMessage& shape, const uint8_t* data);
    void SendCursorPosition(const CursorPositionMessage& pos);

    void SetSignalingSender(std::function<void(const std::string&)> callback) {
        signalingSend_ = std::move(callback);
    }
    void SetInputCallback(std::function<void(const uint8_t* data, size_t size)> callback) {
        inputCallback_ = std::move(callback);
    }
    void SetControlCallback(std::function<void(ControlCommandType)> callback) {
        controlCallback_ = std::move(callback);
    }
    void SetClientConfigCallback(std::function<void(const ClientConfigMessage&)> callback) {
        clientConfigCallback_ = std::move(callback);
    }
    void SetBitrateChangeCallback(std::function<void(uint32_t)> callback) {
        bitrateChangeCallback_ = std::move(callback);
    }

    bool IsPeerConnected() const { return peerConnected_; }

private:
    void CreatePeerConnection();
    void SetupDataChannels();
    void SendRtpPacket(const uint8_t* payload, size_t payloadSize, bool marker, uint32_t rtpTimestamp);
    void OnNetworkFeedback(const NetworkFeedbackMessage& fb);
    void RetransmitPackets(const NackMessage& nack);

    struct StoredRtpPacket {
        uint16_t seq{0};
        std::vector<uint8_t> data;
    };
    std::deque<StoredRtpPacket> retransmitBuffer_;
    std::mutex retransmitMutex_;
    static constexpr size_t kMaxRetransmitBufferSize = 1024;
    static constexpr uint16_t kMaxNackCount = 64;

    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::Track> videoTrack_;
    
    std::shared_ptr<rtc::DataChannel> inputChannel_;
    std::shared_ptr<rtc::DataChannel> audioChannel_;
    std::shared_ptr<rtc::DataChannel> cursorChannel_;
    std::shared_ptr<rtc::DataChannel> controlChannel_;

    std::vector<uint8_t> cursorPayloadBuffer_;
    std::vector<uint8_t> rtpPacketBuffer_;
    std::atomic<bool> peerConnected_{false};
    bool hasRemoteDescription_{false};
    std::vector<std::pair<std::string, std::string>> pendingCandidates_;

    uint16_t rtpSequenceNumber_{0};
    uint32_t rtpSsrc_{0x12345678};

    uint32_t currentBitrateBps_{20'000'000};
    uint32_t maxBitrateBps_{35'000'000};
    static constexpr uint32_t kMinBitrateBps = 8'000'000;
    std::chrono::steady_clock::time_point lastBitrateIncreaseTime_;

    std::function<void(const std::string&)> signalingSend_;
    std::function<void(const uint8_t* data, size_t size)> inputCallback_;
    std::function<void(ControlCommandType)> controlCallback_;
    std::function<void(const ClientConfigMessage&)> clientConfigCallback_;
    std::function<void(uint32_t)> bitrateChangeCallback_;
};