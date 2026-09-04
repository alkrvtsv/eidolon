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
    void RequestIDR();

    void SetSignalingSender(std::function<void(const std::string&)> callback) {
        signalingSend_ = std::move(callback);
    }
    void SetVideoCallback(std::function<void(const uint8_t* data, size_t size)> callback) {
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
    void ProcessVideoChunk(const uint8_t* data, size_t size);
    static bool IsKeyframe(const uint8_t* data, size_t size);

    std::shared_ptr<rtc::PeerConnection> pc_;
    
    std::shared_ptr<rtc::DataChannel> videoChannel_;
    std::shared_ptr<rtc::DataChannel> inputChannel_;
    std::shared_ptr<rtc::DataChannel> audioChannel_;
    std::shared_ptr<rtc::DataChannel> cursorChannel_;
    std::shared_ptr<rtc::DataChannel> controlChannel_;

    std::atomic<bool> connected_{false};
    bool hasRemoteDescription_{false};
    std::vector<std::pair<std::string, std::string>> pendingCandidates_;

    uint32_t activeFrameId_{0};
    uint32_t expectedFrameSize_{0};
    uint16_t totalChunks_{0};
    uint16_t receivedChunksCount_{0};
    bool frameCompleted_{false};
    bool waitingForIDR_{true};
    std::vector<uint8_t> frameBuffer_;
    std::vector<bool> receivedChunksMask_;

    std::function<void(const std::string&)> signalingSend_;
    std::function<void(const uint8_t* data, size_t size)> videoCallback_;
    std::function<void(const uint8_t* data, size_t size)> audioCallback_;
    std::function<void(const CursorShapeMessage&, const uint8_t*)> cursorShapeCallback_;
    std::function<void(const CursorPositionMessage&)> cursorPositionCallback_;
};