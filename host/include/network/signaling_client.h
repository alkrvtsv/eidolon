#pragma once

#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <functional>

using json = nlohmann::json;

class SignalingClient {
public:
    explicit SignalingClient(std::string url);
    ~SignalingClient() noexcept;

    void Connect();
    void Disconnect() noexcept;
    void SendText(const std::string& message);

    void SetOnMessageCallback(std::function<void(const std::string&)> callback) {
        onMessageReceived_ = std::move(callback);
    }

    void SetOnStateChangeCallback(std::function<void(bool connected)> callback) {
        onStateChange_ = std::move(callback);
    }

private:
    std::string serverUrl_;
    std::shared_ptr<rtc::WebSocket> ws_;
    std::function<void(const std::string&)> onMessageReceived_;
    std::function<void(bool)> onStateChange_;
};