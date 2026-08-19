#include "NVENCEncoder.h"
#include <windows.h>

NVENCEncoder::NVENCEncoder() : hInstNvEnc(nullptr), hEncoder(nullptr) {
    memset(&nvenc, 0, sizeof(NV_ENCODE_API_FUNCTION_LIST));
}

NVENCEncoder::~NVENCEncoder() {
    Destroy();
}

bool NVENCEncoder::LoadNvEncApi() {
    hInstNvEnc = LoadLibrary(TEXT("nvEncodeAPI64.dll"));
    if (!hInstNvEnc) {
        std::cerr << "[-] NVENC: Ошибка загрузки nvEncodeAPI64.dll. Установлен ли драйвер NVIDIA?" << std::endl;
        return false;
    }

    typedef NVENCSTATUS(NVENCAPI *NvEncodeAPICreateInstance_Type)(NV_ENCODE_API_FUNCTION_LIST*);
    NvEncodeAPICreateInstance_Type NvEncodeAPICreateInstance = 
        (NvEncodeAPICreateInstance_Type)GetProcAddress(hInstNvEnc, "NvEncodeAPICreateInstance");
    
    if (!NvEncodeAPICreateInstance) {
        std::cerr << "[-] NVENC: Функция NvEncodeAPICreateInstance не найдена." << std::endl;
        return false;
    }

    nvenc.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (NvEncodeAPICreateInstance(&nvenc) != NV_ENC_SUCCESS) {
        std::cerr << "[-] NVENC: Не удалось инициализировать NVENC API." << std::endl;
        return false;
    }

    return true;
}

bool NVENCEncoder::Init(ID3D11Device* d3dDevice, uint32_t width, uint32_t height) {
    frameWidth = width;
    frameHeight = height;

    if (!LoadNvEncApi()) return false;

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sessionParams = { NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER };
    sessionParams.device = d3dDevice;
    sessionParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    sessionParams.apiVersion = NVENCAPI_VERSION;

    NVENCSTATUS status = nvenc.nvEncOpenEncodeSessionEx(&sessionParams, &hEncoder);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "[-] NVENC: Ошибка открытия сессии кодирования. Код: " << status << std::endl;
        return false;
    }

    NV_ENC_INITIALIZE_PARAMS initParams;
    memset(&initParams, 0, sizeof(initParams));
    initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
    initParams.presetGUID = NV_ENC_PRESET_P3_GUID;         
    initParams.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY; 
    
    initParams.encodeWidth = frameWidth & ~1;   
    initParams.encodeHeight = frameHeight & ~1;
    initParams.darWidth = initParams.encodeWidth;
    initParams.darHeight = initParams.encodeHeight;
    
    initParams.frameRateNum = 60;
    initParams.frameRateDen = 1;
    initParams.enableEncodeAsync = 0;
    initParams.enablePTD = 1; 

    NV_ENC_PRESET_CONFIG presetConfig;
    memset(&presetConfig, 0, sizeof(presetConfig));
    presetConfig.version = NV_ENC_PRESET_CONFIG_VER;
    presetConfig.presetCfg.version = NV_ENC_CONFIG_VER;
    
    status = nvenc.nvEncGetEncodePresetConfigEx(hEncoder, initParams.encodeGUID, initParams.presetGUID, initParams.tuningInfo, &presetConfig);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "[-] NVENC: Ошибка получения пресета. Код: " << status << std::endl;
        return false;
    }

    presetConfig.presetCfg.gopLength = 60; 
    presetConfig.presetCfg.frameIntervalP = 1;                    
    
    presetConfig.presetCfg.encodeCodecConfig.h264Config.idrPeriod = 60;
    presetConfig.presetCfg.encodeCodecConfig.h264Config.repeatSPSPPS = 1; 

    presetConfig.presetCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    presetConfig.presetCfg.rcParams.averageBitRate = 15000000;   
    presetConfig.presetCfg.rcParams.maxBitRate = 15000000;
    presetConfig.presetCfg.rcParams.vbvBufferSize = 1500000;     
    presetConfig.presetCfg.rcParams.vbvInitialDelay = 750000;

    initParams.encodeConfig = &presetConfig.presetCfg;

    status = nvenc.nvEncInitializeEncoder(hEncoder, &initParams);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "[-] NVENC: Ошибка инициализации H.264 ядра. Код: " << status << std::endl;
        return false;
    }

    std::cout << "[+] NVENC: Аппаратный кодировщик NVIDIA успешно инициализирован!" << std::endl;

    NV_ENC_CREATE_BITSTREAM_BUFFER bitstreamParams = { NV_ENC_CREATE_BITSTREAM_BUFFER_VER };
    if (nvenc.nvEncCreateBitstreamBuffer(hEncoder, &bitstreamParams) == NV_ENC_SUCCESS) {
        bitstreamBuffer = bitstreamParams.bitstreamBuffer;
    } else {
        std::cerr << "[-] NVENC: Ошибка выделения bitstream-буфера." << std::endl;
        return false;
    }

    return true;
}

void NVENCEncoder::Destroy() {
    if (hEncoder && nvenc.nvEncDestroyEncoder) {
        nvenc.nvEncDestroyEncoder(hEncoder);
        if (hEncoder && bitstreamBuffer) {
        nvenc.nvEncDestroyBitstreamBuffer(hEncoder, bitstreamBuffer);
        bitstreamBuffer = nullptr;
    }
        hEncoder = nullptr;
    }
    if (hInstNvEnc) {
        FreeLibrary(hInstNvEnc);
        hInstNvEnc = nullptr;
    }
}

std::vector<uint8_t> NVENCEncoder::EncodeFrame(ID3D11Texture2D* pTexture) {
    std::vector<uint8_t> packetData;
    if (!hEncoder || !pTexture) return packetData;

    memset(&registeredResource, 0, sizeof(registeredResource));
    registeredResource.version = NV_ENC_REGISTER_RESOURCE_VER;
    registeredResource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    registeredResource.resourceToRegister = (void*)pTexture;
    registeredResource.width = frameWidth;
    registeredResource.height = frameHeight;
    registeredResource.pitch = 0; 
    registeredResource.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB; 
    
    if (nvenc.nvEncRegisterResource(hEncoder, &registeredResource) != NV_ENC_SUCCESS) {
        return packetData; 
    }

    NV_ENC_MAP_INPUT_RESOURCE mapInputParams = { NV_ENC_MAP_INPUT_RESOURCE_VER };
    mapInputParams.registeredResource = registeredResource.registeredResource;
    nvenc.nvEncMapInputResource(hEncoder, &mapInputParams);

    NV_ENC_CREATE_BITSTREAM_BUFFER bitstreamParams = { NV_ENC_CREATE_BITSTREAM_BUFFER_VER };
    if (nvenc.nvEncCreateBitstreamBuffer(hEncoder, &bitstreamParams) == NV_ENC_SUCCESS) {
        bitstreamBuffer = bitstreamParams.bitstreamBuffer;

        NV_ENC_PIC_PARAMS picParams = { NV_ENC_PIC_PARAMS_VER };
        picParams.inputWidth = frameWidth;
        picParams.inputHeight = frameHeight;
        picParams.inputPitch = 0;
        picParams.inputBuffer = mapInputParams.mappedResource;
        picParams.outputBitstream = bitstreamBuffer;
        picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_ARGB;
        picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

        NVENCSTATUS status = nvenc.nvEncEncodePicture(hEncoder, &picParams);

        if (status == NV_ENC_SUCCESS) {
            NV_ENC_LOCK_BITSTREAM lockBitstream = { NV_ENC_LOCK_BITSTREAM_VER };
            lockBitstream.outputBitstream = bitstreamBuffer;
            if (nvenc.nvEncLockBitstream(hEncoder, &lockBitstream) == NV_ENC_SUCCESS) {
                
                uint8_t* pData = (uint8_t*)lockBitstream.bitstreamBufferPtr;
                packetData.assign(pData, pData + lockBitstream.bitstreamSizeInBytes);
                
                nvenc.nvEncUnlockBitstream(hEncoder, lockBitstream.outputBitstream);
            }
        }
    }

    nvenc.nvEncUnmapInputResource(hEncoder, mapInputParams.mappedResource);
    nvenc.nvEncUnregisterResource(hEncoder, registeredResource.registeredResource);

    return packetData;
}