#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "nvenc/nvEncodeAPI.h"
#include <iostream>
#include <vector>

class NVENCEncoder {
public:
    NVENCEncoder();
    ~NVENCEncoder();

    bool Init(ID3D11Device* d3dDevice, uint32_t width, uint32_t height);
    
    std::vector<uint8_t> EncodeFrame(ID3D11Texture2D* pTexture);
    
    void Destroy();

private:
    HMODULE hInstNvEnc;
    NV_ENCODE_API_FUNCTION_LIST nvenc;
    void* hEncoder;

    uint32_t frameWidth;
    uint32_t frameHeight;

    NV_ENC_REGISTER_RESOURCE registeredResource;
    NV_ENC_OUTPUT_PTR bitstreamBuffer;

    bool LoadNvEncApi();
};