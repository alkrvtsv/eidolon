#include "FFmpegDecoder.h"
#include <iostream>

FFmpegDecoder::FFmpegDecoder() {
    packet = av_packet_alloc();
    frame = av_frame_alloc();
}

FFmpegDecoder::~FFmpegDecoder() {
    if (codecContext) avcodec_free_context(&codecContext);
    if (packet) av_packet_free(&packet);
    if (frame) av_frame_free(&frame);
}

bool FFmpegDecoder::Init() {
    // 1. Ищем декодер H.264
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "[-] FFmpeg: Декодер H.264 не найден!" << std::endl;
        return false;
    }

    // 2. Создаем контекст (настройки)
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) return false;

    // Настраиваем под стриминг с минимальной задержкой
    codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY; // Без буферизации
    codecContext->thread_count = 4;                 // Многопоточное декодирование

    // 3. Запускаем ядро декодера
    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        std::cerr << "[-] FFmpeg: Не удалось открыть декодер." << std::endl;
        return false;
    }

    std::cout << "[+] FFmpeg: H.264 декодер успешно инициализирован." << std::endl;
    return true;
}

bool FFmpegDecoder::DecodeAndRender(const uint8_t* data, size_t size, SDL_Renderer* renderer, SDL_Texture** texture) {
    // --- 1. ПАРСИНГ RTP-ЗАГОЛОВКА (Пропускаем транспортный слой) ---
    if (size < 12) return false; // Слишком маленький пакет

    uint8_t cc = data[0] & 0x0F;                // Количество CSRC (обычно 0)
    bool has_extension = (data[0] & 0x10) != 0; // Есть ли расширение заголовка
    size_t offset = 12 + cc * 4;                // Базовый размер RTP-заголовка

    // Пропускаем расширения, если они есть
    if (has_extension) {
        if (size < offset + 4) return false;
        uint16_t ext_len = (data[offset + 2] << 8) | data[offset + 3];
        offset += 4 + ext_len * 4;
    }

    if (size <= offset) return false;

    // Указываем на чистые данные H.264
    const uint8_t* payload = data + offset;
    size_t payload_size = size - offset;


    // --- 2. РАБОТА С H.264 ПОЛЕЗНОЙ НАГРУЗКОЙ ---
    uint8_t nal_type = payload[0] & 0x1F;
    std::vector<uint8_t> readyFrames;

    // 1. ФРАГМЕНТИРОВАННЫЙ КАДР (FU-A - Тип 28)
    if (nal_type == 28) {
        if (payload_size < 2) return false;
        
        uint8_t fu_header = payload[1];
        bool is_start = fu_header & 0x80;
        bool is_end = fu_header & 0x40;
        uint8_t original_nal = (payload[0] & 0xE0) | (fu_header & 0x1F);

        if (is_start) {
            frameBuffer.clear();
            frameBuffer.push_back(0x00);
            frameBuffer.push_back(0x00);
            frameBuffer.push_back(0x00);
            frameBuffer.push_back(0x01);
            frameBuffer.push_back(original_nal);
            frameBuffer.insert(frameBuffer.end(), payload + 2, payload + payload_size);
            return false; 
        } else {
            frameBuffer.insert(frameBuffer.end(), payload + 2, payload + payload_size);
            if (is_end) {
                readyFrames = frameBuffer; 
            } else {
                return false; 
            }
        }
    } 
    // 2. АГРЕГИРОВАННЫЙ ПАКЕТ (STAP-A - Тип 24)
    else if (nal_type == 24) {
        size_t stap_offset = 1; 
        while (stap_offset + 2 <= payload_size) {
            uint16_t nal_size = (payload[stap_offset] << 8) | payload[stap_offset + 1];
            stap_offset += 2;
            
            if (stap_offset + nal_size > payload_size) break; 
            
            readyFrames.push_back(0x00);
            readyFrames.push_back(0x00);
            readyFrames.push_back(0x00);
            readyFrames.push_back(0x01);
            readyFrames.insert(readyFrames.end(), payload + stap_offset, payload + stap_offset + nal_size);
            stap_offset += nal_size;
        }
    } 
    // 3. ОБЫЧНЫЙ ОДИНОЧНЫЙ КАДР (Типы 1-23)
    else if (nal_type >= 1 && nal_type <= 23) {
        readyFrames.push_back(0x00);
        readyFrames.push_back(0x00);
        readyFrames.push_back(0x00);
        readyFrames.push_back(0x01);
        readyFrames.insert(readyFrames.end(), payload, payload + payload_size);
    } 
    else {
        return false; 
    }

    if (readyFrames.empty()) return false;

    // --- 3. ОТПРАВКА СКЛЕЕННОГО КАДРА В FFMPEG ---
    packet->data = readyFrames.data();
    packet->size = (int)readyFrames.size();

    int response = avcodec_send_packet(codecContext, packet);
    if (response < 0) return false;

    response = avcodec_receive_frame(codecContext, frame);
    
    if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
        return false; 
    } else if (response < 0) {
        return false; 
    }

    if (!*texture || frame->width != codecContext->width || frame->height != codecContext->height) {
        if (*texture) SDL_DestroyTexture(*texture);
        
        *texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_IYUV, 
            SDL_TEXTUREACCESS_STREAMING,
            frame->width,
            frame->height
        );
        std::cout << "[Video] ПЕРВЫЙ КАДР УСПЕШНО РАСКОДИРОВАН! Разрешение: " << frame->width << "x" << frame->height << std::endl;
    }

    SDL_UpdateYUVTexture(
        *texture,
        nullptr,
        frame->data[0], frame->linesize[0],
        frame->data[1], frame->linesize[1],
        frame->data[2], frame->linesize[2]
    );

    return true;
}