#pragma once
#include <vector>
#include <cstdint>
#include <SDL2/SDL.h>

// FFmpeg написан на чистом C, поэтому в C++ его нужно оборачивать так:
extern "C" {
#include <libavcodec/avcodec.h>
}

class FFmpegDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    bool Init();
    
    // Принимает байты из сети, декодирует кадр и обновляет текстуру на видеокарте
    bool DecodeAndRender(const uint8_t* data, size_t size, SDL_Renderer* renderer, SDL_Texture** texture);

private:
    const AVCodec* codec = nullptr;
    AVCodecContext* codecContext = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    std::vector<uint8_t> frameBuffer;
    uint16_t expectedSeq = 0;
    bool haveExpected = false;
};