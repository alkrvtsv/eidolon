#pragma once

#include "encoder/video_encoder.h"
#include "nvEncodeAPI.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <array>

using Microsoft::WRL::ComPtr;

class NVENCEncoder final : public IVideoEncoder {
public:
    NVENCEncoder();
    ~NVENCEncoder() noexcept override;

    bool Initialize(ID3D11Device* device, const EncoderConfig& config) override;
    void Shutdown() noexcept override;

    bool EncodeFrame(ID3D11Texture2D* texture, bool forceIDR) override;
    void SetEncodedFrameCallback(std::function<void(const uint8_t*, size_t)> callback) override {
        encodedCallback_ = std::move(callback);
    }

    uint32_t GetWidth() const override { return config_.width; }
    uint32_t GetHeight() const override { return config_.height; }

private:
    static constexpr int kSlotCount = 4;

    struct ResourceSlot {
        NV_ENC_REGISTERED_PTR registeredResource{nullptr};
        NV_ENC_INPUT_PTR mappedResource{nullptr};
        NV_ENC_OUTPUT_PTR bitstreamBuffer{nullptr};
    };

    HMODULE nvencModule_{nullptr};
    std::unique_ptr<NV_ENCODE_API_FUNCTION_LIST> nvApi_;
    void* encoder_{nullptr};

    ComPtr<ID3D11Device> device_;
    EncoderConfig config_{};
    uint64_t frameIndex_{0};

    std::array<ResourceSlot, kSlotCount> slots_{};
    int currentSlot_{0};

    std::function<void(const uint8_t*, size_t)> encodedCallback_;
};