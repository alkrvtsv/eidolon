#pragma once

#include "protocol.h"
#include <rtc/rtc.hpp>
#include <memory>
#include <functional>
#include <atomic>
#include <string>
#include <vector>
#include <map>
#include <utility>
#include <chrono>

class WebRTCClient {
public:
    WebRTCClient();
    ~WebRTCClient() noexcept;

    bool Initialize();
    void Shutdown() noexcept;

    void ProcessSignalingMessage(const std::string& msg);
    void SendInputData(const uint8_t* data, size_t size);
    void SendClientConfig(const ClientConfigMessage& config);
    void RequestIDR();
    void NotifyFrameDropped();

    void SetSignalingSender(std::function<void(const std::string&)> callback) {
        signalingSend_ = std::move(callback);
    }
    void SetVideoCallback(std::function<void(const uint8_t* data, size_t size, uint64_t timestampUs)> callback) {
        videoCallback_ = std::move(callback);
    }
    void SetAudioCallback(std::function<void(const uint8_t* data, size_t size)> callback) {
        audioCallback_ = std::move(callback);
    }
    void SetCursorShapeCallback(std::function<void(const CursorShapeMessage&, const uint8_t*)> callback) {
        cursorShapeCallback_ = std::move(callback);
    }
    void SetCursorPositionCallback(std::function<void(const CursorPositionMessage&)> callback) {
        cursorPositionCallback_ = std::move(callback);
    }
    void SetOnWaitingForIDRCallback(std::function<void()> callback) {
        onWaitingForIDR_ = std::move(callback);
    }

    bool IsConnected() const { return connected_; }

    float GetIncomingBitrateMbps() const noexcept {
        return incomingBitrateMbps_.load(std::memory_order_relaxed);
    }
    float GetRecentLossRatePct() const noexcept {
        return recentLossRatePct_.load(std::memory_order_relaxed);
    }
    uint64_t GetTotalLostPackets() const noexcept {
        return lostPacketCount_;
    }

private:
    void OnRtpPacketReceived(const uint8_t* data, size_t size);
    void DrainReorderBuffer();
    void ProcessOrderedPacket(const uint8_t* data, size_t size);
    void DispatchAssembledFrame();
    void CheckAndSendFeedback();
    void RecreatePeerConnection();
    bool ContainsKeyframeNal() const;

    static bool SequenceLessThan(uint16_t s1, uint16_t s2) noexcept {
        return static_cast<int16_t>(s1 - s2) < 0;
    }

    struct SequenceComparator {
        bool operator()(uint16_t s1, uint16_t s2) const noexcept {
            return SequenceLessThan(s1, s2);
        }
    };

    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::Track> videoTrack_;

    std::shared_ptr<rtc::DataChannel> inputChannel_;
    std::shared_ptr<rtc::DataChannel> audioChannel_;
    std::shared_ptr<rtc::DataChannel> cursorChannel_;
    std::shared_ptr<rtc::DataChannel> controlChannel_;

    std::atomic<bool> connected_{false};
    bool hasRemoteDescription_{false};
    uint64_t peerConnectionGeneration_{0};
    bool restartRequested_{false};
    std::vector<std::pair<std::string, std::string>> pendingCandidates_;

    ClientConfigMessage pendingConfig_{};
    bool hasPendingConfig_{false};

    std::map<uint16_t, std::vector<uint8_t>, SequenceComparator> reorderBuffer_;
    uint16_t nextExpectedSeq_{0};
    bool hasExpectedSeq_{false};

    std::vector<uint8_t> assembledFrameBuffer_;
    std::vector<uint8_t> fuBuffer_;
    uint32_t currentFrameTimestamp_{0};
    bool hasFrameData_{false};
    bool receivedSpsPps_{false};
    bool isFrameCorrupted_{false};
    bool waitingForIDR_{false};
    bool hasGapDetected_{false};
    std::chrono::steady_clock::time_point gapDetectedTime_{};
    std::chrono::steady_clock::time_point waitingForIDRSince_{};
    std::chrono::steady_clock::time_point lastIdrRequestTime_{};
    static constexpr int64_t kReorderTimeoutMs = 8;
    static constexpr int64_t kIdrWaitSafetyMs = 2000;
    uint64_t lostPacketCount_{0};

    uint32_t periodReceivedPackets_{0};
    uint32_t periodLostPackets_{0};
    std::chrono::steady_clock::time_point lastFeedbackTime_;

    size_t periodByteCount_{0};
    std::chrono::steady_clock::time_point lastBitrateCalcTime_;
    std::atomic<float> incomingBitrateMbps_{0.0f};
    std::atomic<float> recentLossRatePct_{0.0f};

    std::function<void(const std::string&)> signalingSend_;
    std::function<void(const uint8_t* data, size_t size, uint64_t timestampUs)> videoCallback_;
    std::function<void(const uint8_t* data, size_t size)> audioCallback_;
    std::function<void(const CursorShapeMessage&, const uint8_t*)> cursorShapeCallback_;
    std::function<void(const CursorPositionMessage&)> cursorPositionCallback_;
    std::function<void()> onWaitingForIDR_;
};