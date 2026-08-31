#include "encoder/nvenc_encoder.h"

NVENCEncoder::NVENCEncoder() {
    memset(&nvenc_, 0, sizeof(nvenc_));
}

NVENCEncoder::~NVENCEncoder() noexcept {
    Shutdown();
}

bool NVENCEncoder::LoadNvEncApi() {
    nvencLib_ = LoadLibraryW(L"nvEncodeAPI64.dll");
    if (!nvencLib_) {
        return false;
    }

    using NvEncodeAPICreateInstance_t = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);
    auto createInstance = reinterpret_cast<NvEncodeAPICreateInstance_t>(
        GetProcAddress(nvencLib_, "NvEncodeAPICreateInstance")
    );

    if (!createInstance) {
        return false;
    }

    nvenc_.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    return createInstance(&nvenc_) == NV_ENC_SUCCESS;
}

bool NVENCEncoder::Initialize(ID3D11Device* device, const EncoderConfig& config) {
    if (!device || config.width == 0 || config.height == 0) {
        return false;
    }

    Shutdown();

    device_ = device;
    config_ = config;

    if (!LoadNvEncApi()) {
        Shutdown();
        return false;
    }

    if (!CreateEncoderSession()) {
        Shutdown();
        return false;
    }

    if (!InitializeEncoder()) {
        Shutdown();
        return false;
    }

    if (!AllocateResources()) {
        Shutdown();
        return false;
    }

    return true;
}

void NVENCEncoder::Shutdown() noexcept {
    ReleaseResources();

    if (encoder_ && nvenc_.nvEncDestroyEncoder) {
        nvenc_.nvEncDestroyEncoder(encoder_);
        encoder_ = nullptr;
    }

    if (nvencLib_) {
        FreeLibrary(nvencLib_);
        nvencLib_ = nullptr;
    }

    memset(&nvenc_, 0, sizeof(nvenc_));
    device_.Reset();
    currentSlot_ = 0;
}

bool NVENCEncoder::CreateEncoderSession() {
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sessionParams = {};
    sessionParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    sessionParams.device = device_.Get();
    sessionParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    sessionParams.apiVersion = NVENCAPI_VERSION;

    return nvenc_.nvEncOpenEncodeSessionEx(&sessionParams, &encoder_) == NV_ENC_SUCCESS;
}

bool NVENCEncoder::InitializeEncoder() {
    NV_ENC_INITIALIZE_PARAMS initParams = {};
    initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
    initParams.presetGUID = NV_ENC_PRESET_P1_GUID;
    initParams.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
    initParams.encodeWidth = config_.width;
    initParams.encodeHeight = config_.height;
    initParams.darWidth = config_.width;
    initParams.darHeight = config_.height;
    initParams.frameRateNum = config_.frameRateNum;
    initParams.frameRateDen = config_.frameRateDen;
    initParams.enableEncodeAsync = 0;
    initParams.enablePTD = 1;

    NV_ENC_PRESET_CONFIG presetConfig = {};
    presetConfig.version = NV_ENC_PRESET_CONFIG_VER;
    presetConfig.presetCfg.version = NV_ENC_CONFIG_VER;

    NVENCSTATUS status = nvenc_.nvEncGetEncodePresetConfigEx(
        encoder_,
        initParams.encodeGUID,
        initParams.presetGUID,
        initParams.tuningInfo,
        &presetConfig
    );

    if (status != NV_ENC_SUCCESS) {
        return false;
    }

    NV_ENC_CONFIG& encCfg = presetConfig.presetCfg;
    encCfg.gopLength = config_.gopLength;
    encCfg.frameIntervalP = 1;

    encCfg.encodeCodecConfig.h264Config.idrPeriod = config_.gopLength;
    encCfg.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
    encCfg.encodeCodecConfig.h264Config.outputAUD = 1;

    if (config_.enableIntraRefresh) {
        encCfg.encodeCodecConfig.h264Config.enableIntraRefresh = 1;
        encCfg.encodeCodecConfig.h264Config.intraRefreshPeriod = config_.intraRefreshPeriod;
        encCfg.encodeCodecConfig.h264Config.intraRefreshCnt = config_.intraRefreshDuration;
    }

    encCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    encCfg.rcParams.averageBitRate = config_.bitRate;
    encCfg.rcParams.maxBitRate = config_.maxBitRate;
    encCfg.rcParams.vbvBufferSize = config_.vbvBufferSize;
    encCfg.rcParams.vbvInitialDelay = config_.vbvBufferSize;
    encCfg.rcParams.zeroReorderDelay = 1;

    initParams.encodeConfig = &encCfg;

    return nvenc_.nvEncInitializeEncoder(encoder_, &initParams) == NV_ENC_SUCCESS;
}

bool NVENCEncoder::AllocateResources() {
    for (size_t i = 0; i < kNumSlots; ++i) {
        NV_ENC_CREATE_BITSTREAM_BUFFER bitstreamParams = {};
        bitstreamParams.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;

        if (nvenc_.nvEncCreateBitstreamBuffer(encoder_, &bitstreamParams) != NV_ENC_SUCCESS) {
            return false;
        }
        slots_[i].bitstreamBuffer = bitstreamParams.bitstreamBuffer;
    }
    return true;
}

void NVENCEncoder::ReleaseResources() noexcept {
    for (size_t i = 0; i < kNumSlots; ++i) {
        if (slots_[i].registeredResource.registeredResource) {
            nvenc_.nvEncUnregisterResource(encoder_, slots_[i].registeredResource.registeredResource);
            slots_[i].registeredResource.registeredResource = nullptr;
        }
        if (slots_[i].bitstreamBuffer) {
            nvenc_.nvEncDestroyBitstreamBuffer(encoder_, slots_[i].bitstreamBuffer);
            slots_[i].bitstreamBuffer = nullptr;
        }
        slots_[i].registeredTexture = nullptr;
    }
}

bool NVENCEncoder::EncodeFrame(ID3D11Texture2D* pTexture, bool forceIDR) {
    if (!encoder_ || !pTexture) {
        return false;
    }

    ResourceSlot& slot = slots_[currentSlot_];

    if (slot.registeredTexture != pTexture) {
        if (slot.registeredResource.registeredResource) {
            nvenc_.nvEncUnregisterResource(encoder_, slot.registeredResource.registeredResource);
            slot.registeredResource.registeredResource = nullptr;
        }

        memset(&slot.registeredResource, 0, sizeof(slot.registeredResource));
        slot.registeredResource.version = NV_ENC_REGISTER_RESOURCE_VER;
        slot.registeredResource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        slot.registeredResource.resourceToRegister = pTexture;
        slot.registeredResource.width = config_.width;
        slot.registeredResource.height = config_.height;
        slot.registeredResource.pitch = 0;
        slot.registeredResource.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;

        if (nvenc_.nvEncRegisterResource(encoder_, &slot.registeredResource) != NV_ENC_SUCCESS) {
            return false;
        }
        slot.registeredTexture = pTexture;
    }

    NV_ENC_MAP_INPUT_RESOURCE mapParams = {};
    mapParams.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    mapParams.registeredResource = slot.registeredResource.registeredResource;

    if (nvenc_.nvEncMapInputResource(encoder_, &mapParams) != NV_ENC_SUCCESS) {
        return false;
    }

    NV_ENC_PIC_PARAMS picParams = {};
    picParams.version = NV_ENC_PIC_PARAMS_VER;
    picParams.inputWidth = config_.width;
    picParams.inputHeight = config_.height;
    picParams.inputPitch = 0;
    picParams.inputBuffer = mapParams.mappedResource;
    picParams.outputBitstream = slot.bitstreamBuffer;
    picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

    if (forceIDR) {
        picParams.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    }

    NVENCSTATUS status = nvenc_.nvEncEncodePicture(encoder_, &picParams);

    if (status == NV_ENC_SUCCESS) {
        NV_ENC_LOCK_BITSTREAM lockParams = {};
        lockParams.version = NV_ENC_LOCK_BITSTREAM_VER;
        lockParams.outputBitstream = slot.bitstreamBuffer;
        lockParams.doNotWait = 0;

        if (nvenc_.nvEncLockBitstream(encoder_, &lockParams) == NV_ENC_SUCCESS) {
            if (encodedFrameCallback_) {
                encodedFrameCallback_(
                    static_cast<const uint8_t*>(lockParams.bitstreamBufferPtr),
                    lockParams.bitstreamSizeInBytes
                );
            }
            nvenc_.nvEncUnlockBitstream(encoder_, lockParams.outputBitstream);
        }
    }

    nvenc_.nvEncUnmapInputResource(encoder_, mapParams.mappedResource);
    currentSlot_ = (currentSlot_ + 1) % kNumSlots;

    return status == NV_ENC_SUCCESS;
}