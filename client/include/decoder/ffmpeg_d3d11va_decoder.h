#pragma once

#include "decoder/video_decoder.h"
#include <wrl/client.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

using Microsoft::WRL::ComPtr;

class FFmpegD3D11VADecoder final : public IVideoDecoder {
public:
    FFmpegD3D11VADecoder();
    ~FFmpegD3D11VADecoder() noexcept override;

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context) override;
    void Shutdown() noexcept override;

    bool Decode(const uint8_t* data, size_t size) override;
    void SetFrameCallback(std::function<void(const DecodedFrame&)> callback) override {
        frameCallback_ = std::move(callback);
    }

private:
    static AVPixelFormat GetHwFormat(AVCodecContext* ctx, const AVPixelFormat* pix_fmts);
    bool InitializeHardwareContext();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;

    AVBufferRef* hwDeviceCtx_{nullptr};
    AVCodecContext* codecContext_{nullptr};
    AVCodecParserContext* parserContext_{nullptr};
    const AVCodec* codec_{nullptr};
    AVPacket* packet_{nullptr};
    AVFrame* frame_{nullptr};

    std::function<void(const DecodedFrame&)> frameCallback_;
};