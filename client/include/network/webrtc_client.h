#pragma once

#include "protocol.h"
#include <rtc/rtc.hpp>
#include <memory>
#include <functional>
#include <atomic>
#include <string>
#include <vector>
#include <utility>

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

    bool IsConnected() const { return connected_; }

private:
    void ProcessRtpPacket(const uint8_t* data, size_t size);
    void DispatchAssembledFrame();

    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::Track> videoTrack_;

    std::shared_ptr<rtc::DataChannel> inputChannel_;
    std::shared_ptr<rtc::DataChannel> audioChannel_;
    std::shared_ptr<rtc::DataChannel> cursorChannel_;
    std::shared_ptr<rtc::DataChannel> controlChannel_;

    std::atomic<bool> connected_{false};
    bool hasRemoteDescription_{false};
    std::vector<std::pair<std::string, std::string>> pendingCandidates_;

    ClientConfigMessage pendingConfig_{};
    bool hasPendingConfig_{false};

    std::vector<uint8_t> assembledFrameBuffer_;
    std::vector<uint8_t> fuBuffer_;
    uint32_t currentFrameTimestamp_{0};
    bool hasFrameData_{false};
    bool receivedSpsPps_{false};

    std::function<void(const std::string&)> signalingSend_;
    std::function<void(const uint8_t* data, size_t size, uint64_t timestampUs)> videoCallback_;
    std::function<void(const uint8_t* data, size_t size)> audioCallback_;
    std::function<void(const CursorShapeMessage&, const uint8_t*)> cursorShapeCallback_;
    std::function<void(const CursorPositionMessage&)> cursorPositionCallback_;
};