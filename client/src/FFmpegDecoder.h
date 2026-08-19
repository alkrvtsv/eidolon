#pragma once
#include <vector>
#include <cstdint>
#include <SDL2/SDL.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

class FFmpegDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    bool Init();
    
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