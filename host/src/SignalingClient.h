#pragma once
#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include <memory>
#include <functional> 

using json = nlohmann::json;

class SignalingClient {
public:
    SignalingClient(const std::string& url);
    ~SignalingClient();

    void Connect();
    void Stop();
    
    void SendMsg(const std::string& message);

    std::function<void(const std::string&)> onMessageReceived;

private:
    std::string serverUrl;
    std::shared_ptr<rtc::WebSocket> ws;
};