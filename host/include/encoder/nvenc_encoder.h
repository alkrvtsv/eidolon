#pragma once

#include "encoder/video_encoder.h"
#include "nvEncodeAPI.h"
#include <windows.h>
#include <wrl/client.h>
#include <functional>

using Microsoft::WRL::ComPtr;

class NVENCEncoder final : public IVideoEncoder {
public:
    NVENCEncoder();
    ~NVENCEncoder() noexcept override;

    bool Initialize(ID3D11Device* device, const EncoderConfig& config) override;
    void Shutdown() noexcept override;

    bool EncodeFrame(ID3D11Texture2D* pTexture, bool forceIDR) override;
    void SetEncodedFrameCallback(std::function<void(const uint8_t* data, size_t size)> callback) override {
        encodedFrameCallback_ = std::move(callback);
    }

    uint32_t GetWidth() const override { return config_.width; }
    uint32_t GetHeight() const override { return config_.height; }

private:
    bool LoadNvEncApi();
    bool CreateEncoderSession();
    bool InitializeEncoder();
    bool AllocateResources();
    void ReleaseResources() noexcept;

    HMODULE nvencLib_{nullptr};
    NV_ENCODE_API_FUNCTION_LIST nvenc_{};
    void* encoder_{nullptr};

    ComPtr<ID3D11Device> device_;
    EncoderConfig config_{};

    static constexpr size_t kNumSlots = 4;
    struct ResourceSlot {
        NV_ENC_REGISTER_RESOURCE registeredResource{};
        NV_ENC_OUTPUT_PTR bitstreamBuffer{nullptr};
        ID3D11Texture2D* registeredTexture{nullptr};
    };

    ResourceSlot slots_[kNumSlots]{};
    size_t currentSlot_{0};

    std::function<void(const uint8_t* data, size_t size)> encodedFrameCallback_;
};