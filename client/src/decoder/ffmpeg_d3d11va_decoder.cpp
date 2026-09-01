#include "decoder/ffmpeg_d3d11va_decoder.h"
#include <iostream>

FFmpegD3D11VADecoder::FFmpegD3D11VADecoder() = default;

FFmpegD3D11VADecoder::~FFmpegD3D11VADecoder() noexcept {
    Shutdown();
}

AVPixelFormat FFmpegD3D11VADecoder::GetHwFormat(AVCodecContext* /*ctx*/, const AVPixelFormat* pix_fmts) {
    while (*pix_fmts != AV_PIX_FMT_NONE) {
        if (*pix_fmts == AV_PIX_FMT_D3D11) {
            return AV_PIX_FMT_D3D11;
        }
        pix_fmts++;
    }
    return AV_PIX_FMT_NONE;
}

bool FFmpegD3D11VADecoder::InitializeHardwareContext() {
    hwDeviceCtx_ = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!hwDeviceCtx_) {
        return false;
    }

    auto* hwctx = reinterpret_cast<AVHWDeviceContext*>(hwDeviceCtx_->data);
    auto* d3d11ctx = reinterpret_cast<AVD3D11VADeviceContext*>(hwctx->hwctx);

    device_->AddRef();
    d3d11ctx->device = device_.Get();
    d3d11ctx->lock = nullptr;
    d3d11ctx->unlock = nullptr;

    if (av_hwdevice_ctx_init(hwDeviceCtx_) < 0) {
        av_buffer_unref(&hwDeviceCtx_);
        return false;
    }

    return true;
}

bool FFmpegD3D11VADecoder::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!device || !context) {
        return false;
    }

    Shutdown();

    device_ = device;
    context_ = context;

    codec_ = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec_) {
        Shutdown();
        return false;
    }

    parserContext_ = av_parser_init(codec_->id);
    if (!parserContext_) {
        Shutdown();
        return false;
    }

    codecContext_ = avcodec_alloc_context3(codec_);
    if (!codecContext_) {
        Shutdown();
        return false;
    }

    if (!InitializeHardwareContext()) {
        Shutdown();
        return false;
    }

    codecContext_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);
    codecContext_->get_format = GetHwFormat;
    codecContext_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codecContext_->flags2 |= AV_CODEC_FLAG2_FAST;
    codecContext_->thread_count = 1;

    if (avcodec_open2(codecContext_, codec_, nullptr) < 0) {
        Shutdown();
        return false;
    }

    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();

    if (!packet_ || !frame_) {
        Shutdown();
        return false;
    }

    return true;
}

void FFmpegD3D11VADecoder::Shutdown() noexcept {
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }

    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }

    if (codecContext_) {
        avcodec_free_context(&codecContext_);
        codecContext_ = nullptr;
    }

    if (parserContext_) {
        av_parser_close(parserContext_);
        parserContext_ = nullptr;
    }

    if (hwDeviceCtx_) {
        av_buffer_unref(&hwDeviceCtx_);
        hwDeviceCtx_ = nullptr;
    }

    codec_ = nullptr;
    context_.Reset();
    device_.Reset();
}

bool FFmpegD3D11VADecoder::Decode(const uint8_t* data, size_t size) {
    if (!codecContext_ || !packet_ || !frame_ || !data || size == 0) {
        return false;
    }

    uint8_t* parseData = const_cast<uint8_t*>(data);
    int parseSize = static_cast<int>(size);

    while (parseSize > 0) {
        int parsedBytes = av_parser_parse2(
            parserContext_,
            codecContext_,
            &packet_->data,
            &packet_->size,
            parseData,
            parseSize,
            AV_NOPTS_VALUE,
            AV_NOPTS_VALUE,
            0
        );

        if (parsedBytes < 0) {
            return false;
        }

        parseData += parsedBytes;
        parseSize -= parsedBytes;

        if (packet_->size == 0) {
            continue;
        }

        int sendResult = avcodec_send_packet(codecContext_, packet_);
        if (sendResult < 0) {
            av_packet_unref(packet_);
            return false;
        }

        while (sendResult >= 0) {
            int receiveResult = avcodec_receive_frame(codecContext_, frame_);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }

            if (receiveResult < 0) {
                av_packet_unref(packet_);
                return false;
            }

            if (frame_->format == AV_PIX_FMT_D3D11 && frameCallback_) {
                auto* texture = reinterpret_cast<ID3D11Texture2D*>(frame_->data[0]);
                auto subresourceIndex = static_cast<uint32_t>(reinterpret_cast<intptr_t>(frame_->data[1]));

                DecodedFrame decodedFrame;
                decodedFrame.texture = texture;
                decodedFrame.subresourceIndex = subresourceIndex;
                decodedFrame.width = static_cast<uint32_t>(frame_->width);
                decodedFrame.height = static_cast<uint32_t>(frame_->height);

                frameCallback_(decodedFrame);
            }

            av_frame_unref(frame_);
        }

        av_packet_unref(packet_);
    }

    return true;
}