#include "network/signaling_client.h"

SignalingClient::SignalingClient(std::string url) : serverUrl_(std::move(url)) {
    ws_ = std::make_shared<rtc::WebSocket>();

    ws_->onOpen([this]() {
        json regMsg = {{"type", "register"}, {"role", "host"}};
        ws_->send(regMsg.dump());
        if (onStateChange_) {
            onStateChange_(true);
        }
    });

    ws_->onMessage([this](std::variant<rtc::binary, std::string> data) {
        if (std::holds_alternative<std::string>(data)) {
            if (onMessageReceived_) {
                onMessageReceived_(std::get<std::string>(data));
            }
        }
    });

    ws_->onClosed([this]() {
        if (onStateChange_) {
            onStateChange_(false);
        }
    });

    ws_->onError([this](const std::string&) {
        if (onStateChange_) {
            onStateChange_(false);
        }
    });
}

SignalingClient::~SignalingClient() noexcept {
    Disconnect();
}

void SignalingClient::Connect() {
    if (ws_) {
        ws_->open(serverUrl_);
    }
}

void SignalingClient::Disconnect() noexcept {
    if (ws_) {
        ws_->close();
    }
}

void SignalingClient::SendText(const std::string& message) {
    if (ws_ && ws_->isOpen()) {
        ws_->send(message);
    }
}