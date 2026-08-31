#pragma once

#include "protocol.h"
#include <rtc/rtc.hpp>
#include <memory>
#include <functional>
#include <atomic>
#include <vector>

class WebRTCStreamer {
public:
    WebRTCStreamer();
    ~WebRTCStreamer() noexcept;

    bool Initialize();
    void Shutdown() noexcept;

    void ProcessSignalingMessage(const std::string& msg);
    
    void SendVideoFrame(const uint8_t* data, size_t size);
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

    bool IsConnected() const { return peerConnected_; }

private:
    void SetupDataChannels();

    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::Track> videoTrack_;
    std::shared_ptr<rtc::DataChannel> inputChannel_;
    std::shared_ptr<rtc::DataChannel> audioChannel_;
    std::shared_ptr<rtc::DataChannel> cursorChannel_;
    std::shared_ptr<rtc::DataChannel> controlChannel_;

    std::atomic<bool> peerConnected_{false};
    std::vector<uint8_t> cursorPayloadBuffer_;

    std::function<void(const std::string&)> signalingSend_;
    std::function<void(const uint8_t* data, size_t size)> inputCallback_;
    std::function<void(ControlCommandType)> controlCallback_;
};