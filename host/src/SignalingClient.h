#pragma once
#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include <memory>
#include <functional> // Добавили для std::function

using json = nlohmann::json;

class SignalingClient {
public:
    SignalingClient(const std::string& url);
    ~SignalingClient();

    void Connect();
    void Stop();
    
    // Новый метод для отправки сообщений на сервер
    void SendMsg(const std::string& message);

    // Коллбек для передачи полученных сообщений в main.cpp
    std::function<void(const std::string&)> onMessageReceived;

private:
    std::string serverUrl;
    std::shared_ptr<rtc::WebSocket> ws;
};