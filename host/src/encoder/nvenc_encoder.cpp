#include "encoder/nvenc_encoder.h"

NVENCEncoder::NVENCEncoder() = default;

NVENCEncoder::~NVENCEncoder() noexcept {
    Shutdown();
}

bool NVENCEncoder::Initialize(ID3D11Device* device, const EncoderConfig& config) {
    if (!device) {
        return false;
    }

    Shutdown();
    device_ = device;
    config_ = config;

    nvencModule_ = LoadLibraryW(L"nvEncodeAPI64.dll");
    if (!nvencModule_) {
        return false;
    }

    typedef NVENCSTATUS(NVENCAPI* PNVENCODEAPICREATEINSTANCE)(NV_ENCODE_API_FUNCTION_LIST*);
    auto NvEncodeAPICreateInstance = reinterpret_cast<PNVENCODEAPICREATEINSTANCE>(
        GetProcAddress(nvencModule_, "NvEncodeAPICreateInstance")
    );

    if (!NvEncodeAPICreateInstance) {
        Shutdown();
        return false;
    }

    nvApi_ = std::make_unique<NV_ENCODE_API_FUNCTION_LIST>();
    nvApi_->version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (NvEncodeAPICreateInstance(nvApi_.get()) != NV_ENC_SUCCESS) {
        Shutdown();
        return false;
    }

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS openSessionParams = {};
    openSessionParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    openSessionParams.device = device_.Get();
    openSessionParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    openSessionParams.apiVersion = NVENCAPI_VERSION;

    if (nvApi_->nvEncOpenEncodeSessionEx(&openSessionParams, &encoder_) != NV_ENC_SUCCESS) {
        Shutdown();
        return false;
    }

    NV_ENC_PRESET_CONFIG presetConfig = {};
    presetConfig.version = NV_ENC_PRESET_CONFIG_VER;
    presetConfig.presetCfg.version = NV_ENC_CONFIG_VER;

    GUID codecGuid = NV_ENC_CODEC_H264_GUID;
    GUID presetGuid = NV_ENC_PRESET_P3_GUID;

    if (nvApi_->nvEncGetEncodePresetConfigEx(encoder_, codecGuid, presetGuid, NV_ENC_TUNING_INFO_HIGH_QUALITY, &presetConfig) != NV_ENC_SUCCESS) {
        Shutdown();
        return false;
    }

    NV_ENC_CONFIG encodeConfig = presetConfig.presetCfg;
    encodeConfig.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
    encodeConfig.gopLength = NVENC_INFINITE_GOPLENGTH;
    encodeConfig.frameIntervalP = 1;

    encodeConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    encodeConfig.rcParams.averageBitRate = config_.bitRate;
    encodeConfig.rcParams.maxBitRate = config_.maxBitRate;
    encodeConfig.rcParams.vbvBufferSize = config_.vbvBufferSize;
    encodeConfig.rcParams.vbvInitialDelay = config_.vbvBufferSize;
    encodeConfig.rcParams.zeroReorderDelay = 1;

    encodeConfig.rcParams.enableAQ = 1;
    encodeConfig.rcParams.aqStrength = 8;
    encodeConfig.rcParams.enableTemporalAQ = 1;

    encodeConfig.rcParams.enableMinQP = 1;
    encodeConfig.rcParams.enableMaxQP = 1;
    encodeConfig.rcParams.minQP = { 6, 6, 6 };
    encodeConfig.rcParams.maxQP = { 24, 24, 24 };

    encodeConfig.encodeCodecConfig.h264Config.enableIntraRefresh = 0;
    encodeConfig.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
    encodeConfig.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
    encodeConfig.encodeCodecConfig.h264Config.sliceMode = 0;
    encodeConfig.encodeCodecConfig.h264Config.sliceModeData = 0;

    encodeConfig.encodeCodecConfig.h264Config.h264VUIParameters.videoFormat = NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;
    encodeConfig.encodeCodecConfig.h264Config.h264VUIParameters.colourDescriptionPresentFlag = 1;
    encodeConfig.encodeCodecConfig.h264Config.h264VUIParameters.colourPrimaries = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
    encodeConfig.encodeCodecConfig.h264Config.h264VUIParameters.transferCharacteristics = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
    encodeConfig.encodeCodecConfig.h264Config.h264VUIParameters.colourMatrix = NV_ENC_VUI_MATRIX_COEFFS_BT709;
    encodeConfig.encodeCodecConfig.h264Config.h264VUIParameters.videoFullRangeFlag = 1;

    NV_ENC_INITIALIZE_PARAMS initParams = {};
    initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initParams.encodeGUID = codecGuid;
    initParams.presetGUID = presetGuid;
    initParams.encodeWidth = config_.width;
    initParams.encodeHeight = config_.height;
    initParams.darWidth = config_.width;
    initParams.darHeight = config_.height;
    initParams.frameRateNum = config_.frameRateNum;
    initParams.frameRateDen = config_.frameRateDen;
    initParams.enablePTD = 1;
    initParams.encodeConfig = &encodeConfig;
    initParams.tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY;

    if (nvApi_->nvEncInitializeEncoder(encoder_, &initParams) != NV_ENC_SUCCESS) {
        Shutdown();
        return false;
    }

    for (int i = 0; i < kSlotCount; ++i) {
        NV_ENC_CREATE_BITSTREAM_BUFFER createBitstream = {};
        createBitstream.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        if (nvApi_->nvEncCreateBitstreamBuffer(encoder_, &createBitstream) != NV_ENC_SUCCESS) {
            Shutdown();
            return false;
        }
        slots_[i].bitstreamBuffer = createBitstream.bitstreamBuffer;
    }

    return true;
}

void NVENCEncoder::Shutdown() noexcept {
    if (nvApi_ && encoder_) {
        for (auto& slot : slots_) {
            if (slot.registeredResource) {
                if (slot.mappedResource) {
                    nvApi_->nvEncUnmapInputResource(encoder_, slot.mappedResource);
                    slot.mappedResource = nullptr;
                }
                nvApi_->nvEncUnregisterResource(encoder_, slot.registeredResource);
                slot.registeredResource = nullptr;
            }
            if (slot.bitstreamBuffer) {
                nvApi_->nvEncDestroyBitstreamBuffer(encoder_, slot.bitstreamBuffer);
                slot.bitstreamBuffer = nullptr;
            }
        }
        nvApi_->nvEncDestroyEncoder(encoder_);
        encoder_ = nullptr;
    }

    if (nvencModule_) {
        FreeLibrary(nvencModule_);
        nvencModule_ = nullptr;
    }

    device_.Reset();
}

bool NVENCEncoder::EncodeFrame(ID3D11Texture2D* texture, bool forceIDR) {
    if (!nvApi_ || !encoder_ || !texture) {
        return false;
    }

    auto& slot = slots_[currentSlot_];

    if (slot.registeredResource) {
        if (slot.mappedResource) {
            nvApi_->nvEncUnmapInputResource(encoder_, slot.mappedResource);
            slot.mappedResource = nullptr;
        }
        nvApi_->nvEncUnregisterResource(encoder_, slot.registeredResource);
        slot.registeredResource = nullptr;
    }

    NV_ENC_REGISTER_RESOURCE registerResource = {};
    registerResource.version = NV_ENC_REGISTER_RESOURCE_VER;
    registerResource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    registerResource.resourceToRegister = texture;
    registerResource.width = config_.width;
    registerResource.height = config_.height;
    registerResource.pitch = config_.width;
    registerResource.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;

    if (nvApi_->nvEncRegisterResource(encoder_, &registerResource) != NV_ENC_SUCCESS) {
        return false;
    }
    slot.registeredResource = registerResource.registeredResource;

    NV_ENC_MAP_INPUT_RESOURCE mapInput = {};
    mapInput.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    mapInput.registeredResource = slot.registeredResource;

    if (nvApi_->nvEncMapInputResource(encoder_, &mapInput) != NV_ENC_SUCCESS) {
        return false;
    }
    slot.mappedResource = mapInput.mappedResource;

    NV_ENC_PIC_PARAMS picParams = {};
    picParams.version = NV_ENC_PIC_PARAMS_VER;
    picParams.inputBuffer = slot.mappedResource;
    picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    picParams.inputWidth = config_.width;
    picParams.inputHeight = config_.height;
    picParams.outputBitstream = slot.bitstreamBuffer;
    picParams.inputTimeStamp = frameIndex_++;
    picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

    if (forceIDR) {
        picParams.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    }

    NVENCSTATUS status = nvApi_->nvEncEncodePicture(encoder_, &picParams);
    if (status != NV_ENC_SUCCESS) {
        return false;
    }

    NV_ENC_LOCK_BITSTREAM lockBitstream = {};
    lockBitstream.version = NV_ENC_LOCK_BITSTREAM_VER;
    lockBitstream.outputBitstream = slot.bitstreamBuffer;
    lockBitstream.doNotWait = 0;

    if (nvApi_->nvEncLockBitstream(encoder_, &lockBitstream) == NV_ENC_SUCCESS) {
        if (encodedCallback_ && lockBitstream.bitstreamSizeInBytes > 0) {
            encodedCallback_(reinterpret_cast<const uint8_t*>(lockBitstream.bitstreamBufferPtr), lockBitstream.bitstreamSizeInBytes);
        }
        nvApi_->nvEncUnlockBitstream(encoder_, slot.bitstreamBuffer);
    }

    currentSlot_ = (currentSlot_ + 1) % kSlotCount;
    return true;
}