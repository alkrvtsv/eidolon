#include "SignalingClient.h"

SignalingClient::SignalingClient(const std::string& url) : serverUrl(url) {
    ws = std::make_shared<rtc::WebSocket>();

    ws->onOpen([this]() {
        std::cout << "[Сигналинг] Соединение установлено!" << std::endl;
        json regMsg = {{"type", "register"}, {"role", "host"}};
        ws->send(regMsg.dump());
    });

    ws->onMessage([this](std::variant<rtc::binary, std::string> data) {
        if (std::holds_alternative<std::string>(data)) {
            std::string msg = std::get<std::string>(data);
            std::cout << "[Сигналинг] Входящее сообщение: " << msg << std::endl;
            
            if (onMessageReceived) {
                onMessageReceived(msg);
            }
        }
    });

    ws->onClosed([]() { std::cout << "[Сигналинг] Соединение закрыто." << std::endl; });
    ws->onError([](const std::string& error) { std::cerr << "[-] Ошибка WebSocket: " << error << std::endl; });
}

SignalingClient::~SignalingClient() { Stop(); }

void SignalingClient::Connect() { ws->open(serverUrl); }
void SignalingClient::Stop() { if (ws) ws->close(); }

void SignalingClient::SendMsg(const std::string& message) { 
    if (ws && ws->isOpen()) {
        ws->send(message);
    }
}