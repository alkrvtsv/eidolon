#include "NVENCEncoder.h"
#include <windows.h>

NVENCEncoder::NVENCEncoder() : hInstNvEnc(nullptr), hEncoder(nullptr) {
    memset(&nvenc, 0, sizeof(NV_ENCODE_API_FUNCTION_LIST));
}

NVENCEncoder::~NVENCEncoder() {
    Destroy();
}

bool NVENCEncoder::LoadNvEncApi() {
    // Подгружаем библиотеку драйвера NVIDIA (она всегда лежит в системе)
    hInstNvEnc = LoadLibrary(TEXT("nvEncodeAPI64.dll"));
    if (!hInstNvEnc) {
        std::cerr << "[-] NVENC: Ошибка загрузки nvEncodeAPI64.dll. Установлен ли драйвер NVIDIA?" << std::endl;
        return false;
    }

    // Ищем функцию создания инстанса
    typedef NVENCSTATUS(NVENCAPI *NvEncodeAPICreateInstance_Type)(NV_ENCODE_API_FUNCTION_LIST*);
    NvEncodeAPICreateInstance_Type NvEncodeAPICreateInstance = 
        (NvEncodeAPICreateInstance_Type)GetProcAddress(hInstNvEnc, "NvEncodeAPICreateInstance");
    
    if (!NvEncodeAPICreateInstance) {
        std::cerr << "[-] NVENC: Функция NvEncodeAPICreateInstance не найдена." << std::endl;
        return false;
    }

    // Запрашиваем список всех доступных функций NVENC
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

    // Параметры открытия сессии NVENC 
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sessionParams = { NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER };
    sessionParams.device = d3dDevice;
    sessionParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    sessionParams.apiVersion = NVENCAPI_VERSION;

    NVENCSTATUS status = nvenc.nvEncOpenEncodeSessionEx(&sessionParams, &hEncoder);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "[-] NVENC: Ошибка открытия сессии кодирования. Код: " << status << std::endl;
        return false;
    }

    // 1. Настраиваем базовые параметры H.264 кодека
    NV_ENC_INITIALIZE_PARAMS initParams;
    memset(&initParams, 0, sizeof(initParams));
    initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
    initParams.presetGUID = NV_ENC_PRESET_P3_GUID;          // Стабильный пресет P3
    initParams.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY; // Стандартный Low Latency
    
    // Выравнивание разрешения
    initParams.encodeWidth = frameWidth & ~1;   
    initParams.encodeHeight = frameHeight & ~1;
    initParams.darWidth = initParams.encodeWidth;
    initParams.darHeight = initParams.encodeHeight;
    
    initParams.frameRateNum = 60;
    initParams.frameRateDen = 1;
    initParams.enableEncodeAsync = 0;
    initParams.enablePTD = 1; 

    // 2. Получаем дефолтный конфиг пресета 
    // ЗДЕСЬ ИСПОЛЬЗУЕМ ФУНКЦИЮ С "Ex" НА КОНЦЕ!
    NV_ENC_PRESET_CONFIG presetConfig;
    memset(&presetConfig, 0, sizeof(presetConfig));
    presetConfig.version = NV_ENC_PRESET_CONFIG_VER;
    presetConfig.presetCfg.version = NV_ENC_CONFIG_VER;
    
    status = nvenc.nvEncGetEncodePresetConfigEx(hEncoder, initParams.encodeGUID, initParams.presetGUID, initParams.tuningInfo, &presetConfig);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "[-] NVENC: Ошибка получения пресета. Код: " << status << std::endl;
        return false;
    }

    // 3. ЖЕСТКО задаем параметры битрейта
    presetConfig.presetCfg.gopLength = NVENC_INFINITE_GOPLENGTH; 
    presetConfig.presetCfg.frameIntervalP = 1;                    
    
    presetConfig.presetCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    presetConfig.presetCfg.rcParams.averageBitRate = 15000000;   
    presetConfig.presetCfg.rcParams.maxBitRate = 15000000;
    presetConfig.presetCfg.rcParams.vbvBufferSize = 1500000;     
    presetConfig.presetCfg.rcParams.vbvInitialDelay = 750000;

    initParams.encodeConfig = &presetConfig.presetCfg;

    // 4. ИНИЦИАЛИЗИРУЕМ ЭНКОДЕР 
    // ВОТ ФУНКЦИЯ, КОТОРАЯ БЫЛА УТЕРЯНА!
    status = nvenc.nvEncInitializeEncoder(hEncoder, &initParams);
    if (status != NV_ENC_SUCCESS) {
        std::cerr << "[-] NVENC: Ошибка инициализации H.264 ядра. Код: " << status << std::endl;
        return false;
    }

    std::cout << "[+] NVENC: Аппаратный кодировщик NVIDIA успешно инициализирован!" << std::endl;
    return true;
}

void NVENCEncoder::Destroy() {
    if (hEncoder && nvenc.nvEncDestroyEncoder) {
        nvenc.nvEncDestroyEncoder(hEncoder);
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

    // 1. Регистрируем текстуру от DXGI в NVENC
    memset(&registeredResource, 0, sizeof(registeredResource));
    registeredResource.version = NV_ENC_REGISTER_RESOURCE_VER;
    registeredResource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    registeredResource.resourceToRegister = (void*)pTexture;
    registeredResource.width = frameWidth;
    registeredResource.height = frameHeight;
    registeredResource.pitch = 0; // NVENC сам вычислит для D3D11 текстуры
    registeredResource.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB; // Формат Windows Desktop
    
    if (nvenc.nvEncRegisterResource(hEncoder, &registeredResource) != NV_ENC_SUCCESS) {
        return packetData; // Ошибка регистрации
    }

    // 2. Маппим (проецируем) ресурс для кодировщика
    NV_ENC_MAP_INPUT_RESOURCE mapInputParams = { NV_ENC_MAP_INPUT_RESOURCE_VER };
    mapInputParams.registeredResource = registeredResource.registeredResource;
    nvenc.nvEncMapInputResource(hEncoder, &mapInputParams);

    // 3. Создаем выходной буфер (куда видеокарта положит сжатые данные)
    NV_ENC_CREATE_BITSTREAM_BUFFER bitstreamParams = { NV_ENC_CREATE_BITSTREAM_BUFFER_VER };
    if (nvenc.nvEncCreateBitstreamBuffer(hEncoder, &bitstreamParams) == NV_ENC_SUCCESS) {
        bitstreamBuffer = bitstreamParams.bitstreamBuffer;

        // 4. Настраиваем параметры кадра (указываем, что это обычный кадр, без хитростей)
        NV_ENC_PIC_PARAMS picParams = { NV_ENC_PIC_PARAMS_VER };
        picParams.inputWidth = frameWidth;
        picParams.inputHeight = frameHeight;
        picParams.inputPitch = 0;
        picParams.inputBuffer = mapInputParams.mappedResource;
        picParams.outputBitstream = bitstreamBuffer;
        picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_ARGB;
        picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

        // 5. КОДИРУЕМ! (Аппаратный вызов)
        NVENCSTATUS status = nvenc.nvEncEncodePicture(hEncoder, &picParams);

        if (status == NV_ENC_SUCCESS) {
            // 6. Забираем сжатые байты (NAL units) из видеопамяти
            NV_ENC_LOCK_BITSTREAM lockBitstream = { NV_ENC_LOCK_BITSTREAM_VER };
            lockBitstream.outputBitstream = bitstreamBuffer;
            if (nvenc.nvEncLockBitstream(hEncoder, &lockBitstream) == NV_ENC_SUCCESS) {
                
                // Копируем байты в наш std::vector для передачи в WebRTC
                uint8_t* pData = (uint8_t*)lockBitstream.bitstreamBufferPtr;
                packetData.assign(pData, pData + lockBitstream.bitstreamSizeInBytes);
                
                nvenc.nvEncUnlockBitstream(hEncoder, lockBitstream.outputBitstream);
            }
        }
        // Уничтожаем временный буфер
        nvenc.nvEncDestroyBitstreamBuffer(hEncoder, bitstreamBuffer);
    }

    // Обязательно отвязываем текстуру, чтобы DXGI мог писать в нее следующий кадр
    nvenc.nvEncUnmapInputResource(hEncoder, mapInputParams.mappedResource);
    nvenc.nvEncUnregisterResource(hEncoder, registeredResource.registeredResource);

    return packetData;
}