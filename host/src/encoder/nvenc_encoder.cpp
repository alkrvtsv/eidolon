#include "encoder/nvenc_encoder.h"
#include <iostream>
#include <cstring>

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
    device_->GetImmediateContext(&context_);
    config_ = config;
    currentSlot_ = 0;
    lastValidSlot_ = -1;

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

    GUID codecGuid = NV_ENC_CODEC_HEVC_GUID;
    GUID presetGuid = NV_ENC_PRESET_P3_GUID;

    if (nvApi_->nvEncGetEncodePresetConfigEx(encoder_, codecGuid, presetGuid, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &presetConfig) != NV_ENC_SUCCESS) {
        Shutdown();
        return false;
    }

    encodeConfig_ = presetConfig.presetCfg;
    encodeConfig_.gopLength = NVENC_INFINITE_GOPLENGTH;
    encodeConfig_.frameIntervalP = 1;
    encodeConfig_.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    encodeConfig_.rcParams.averageBitRate = config_.bitRate;
    encodeConfig_.rcParams.maxBitRate = config_.maxBitRate;
    encodeConfig_.rcParams.vbvBufferSize = config_.vbvBufferSize;
    encodeConfig_.rcParams.vbvInitialDelay = config_.vbvBufferSize;
    encodeConfig_.rcParams.zeroReorderDelay = 1;

    if (config_.enableIntraRefresh) {
        encodeConfig_.encodeCodecConfig.hevcConfig.enableIntraRefresh = 1;
        encodeConfig_.encodeCodecConfig.hevcConfig.intraRefreshPeriod = config_.intraRefreshPeriod;
        encodeConfig_.encodeCodecConfig.hevcConfig.intraRefreshCnt = config_.intraRefreshDuration;
    }

    encodeConfig_.encodeCodecConfig.hevcConfig.repeatSPSPPS = 1;
    encodeConfig_.encodeCodecConfig.hevcConfig.idrPeriod = NVENC_INFINITE_GOPLENGTH;
    encodeConfig_.encodeCodecConfig.hevcConfig.sliceMode = 0;
    encodeConfig_.encodeCodecConfig.hevcConfig.sliceModeData = 0;

    encodeConfig_.encodeCodecConfig.hevcConfig.hevcVUIParameters.videoFormat = NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;
    encodeConfig_.encodeCodecConfig.hevcConfig.hevcVUIParameters.colourDescriptionPresentFlag = 1;
    encodeConfig_.encodeCodecConfig.hevcConfig.hevcVUIParameters.colourPrimaries = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
    encodeConfig_.encodeCodecConfig.hevcConfig.hevcVUIParameters.transferCharacteristics = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
    encodeConfig_.encodeCodecConfig.hevcConfig.hevcVUIParameters.colourMatrix = NV_ENC_VUI_MATRIX_COEFFS_BT709;
    encodeConfig_.encodeCodecConfig.hevcConfig.hevcVUIParameters.videoFullRangeFlag = 1;

    initParams_.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initParams_.encodeGUID = codecGuid;
    initParams_.presetGUID = presetGuid;
    initParams_.encodeWidth = config_.width;
    initParams_.encodeHeight = config_.height;
    initParams_.darWidth = config_.width;
    initParams_.darHeight = config_.height;
    initParams_.frameRateNum = config_.frameRateNum;
    initParams_.frameRateDen = config_.frameRateDen;
    initParams_.enablePTD = 1;
    initParams_.encodeConfig = &encodeConfig_;
    initParams_.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;

    if (nvApi_->nvEncInitializeEncoder(encoder_, &initParams_) != NV_ENC_SUCCESS) {
        Shutdown();
        return false;
    }

    currentBitrate_.store(config_.bitRate, std::memory_order_relaxed);

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = config_.width;
    texDesc.Height = config_.height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_NV12;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    D3D11_QUERY_DESC queryDesc = {};
    queryDesc.Query = D3D11_QUERY_EVENT;

    for (int i = 0; i < kSlotCount; ++i) {
        HRESULT hr = device_->CreateTexture2D(&texDesc, nullptr, &slots_[i].inputTexture);
        if (FAILED(hr)) {
            Shutdown();
            return false;
        }

        hr = device_->CreateQuery(&queryDesc, &slots_[i].completionQuery);
        if (FAILED(hr)) {
            Shutdown();
            return false;
        }

        NV_ENC_REGISTER_RESOURCE registerResource = {};
        registerResource.version = NV_ENC_REGISTER_RESOURCE_VER;
        registerResource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        registerResource.resourceToRegister = slots_[i].inputTexture.Get();
        registerResource.width = config_.width;
        registerResource.height = config_.height;
        registerResource.pitch = config_.width;
        registerResource.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;

        if (nvApi_->nvEncRegisterResource(encoder_, &registerResource) != NV_ENC_SUCCESS) {
            Shutdown();
            return false;
        }
        slots_[i].registeredResource = registerResource.registeredResource;

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
            if (slot.mappedResource) {
                nvApi_->nvEncUnmapInputResource(encoder_, slot.mappedResource);
                slot.mappedResource = nullptr;
            }
            if (slot.registeredResource) {
                nvApi_->nvEncUnregisterResource(encoder_, slot.registeredResource);
                slot.registeredResource = nullptr;
            }
            if (slot.bitstreamBuffer) {
                nvApi_->nvEncDestroyBitstreamBuffer(encoder_, slot.bitstreamBuffer);
                slot.bitstreamBuffer = nullptr;
            }
            slot.completionQuery.Reset();
            slot.inputTexture.Reset();
        }
        nvApi_->nvEncDestroyEncoder(encoder_);
        encoder_ = nullptr;
    }

    if (nvencModule_) {
        FreeLibrary(nvencModule_);
        nvencModule_ = nullptr;
    }

    context_.Reset();
    device_.Reset();
    lastValidSlot_ = -1;
    currentSlot_ = 0;
    currentBitrate_.store(0, std::memory_order_relaxed);
}

ID3D11Texture2D* NVENCEncoder::GetNextInputTexture() {
    return slots_[currentSlot_].inputTexture.Get();
}

void NVENCEncoder::SetBitrate(uint32_t bitrateBps) {
    if (!encoder_ || !nvApi_) {
        return;
    }

    if (bitrateBps == currentBitrate_.load(std::memory_order_relaxed)) {
        return;
    }

    NV_ENC_RECONFIGURE_PARAMS reconfigParams = {};
    reconfigParams.version = NV_ENC_RECONFIGURE_PARAMS_VER;
    reconfigParams.reInitEncodeParams.version = NV_ENC_INITIALIZE_PARAMS_VER;

    std::memcpy(&reconfigParams.reInitEncodeParams, &initParams_, sizeof(NV_ENC_INITIALIZE_PARAMS));

    uint32_t fps = config_.frameRateNum ? (config_.frameRateNum / (config_.frameRateDen ? config_.frameRateDen : 1)) : 60;
    if (fps == 0) fps = 60;

    encodeConfig_.rcParams.averageBitRate = bitrateBps;
    encodeConfig_.rcParams.maxBitRate = bitrateBps;
    encodeConfig_.rcParams.vbvBufferSize = (bitrateBps / fps) * 2;
    encodeConfig_.rcParams.vbvInitialDelay = encodeConfig_.rcParams.vbvBufferSize;

    reconfigParams.reInitEncodeParams.encodeConfig = &encodeConfig_;

    NVENCSTATUS status = nvApi_->nvEncReconfigureEncoder(encoder_, &reconfigParams);
    if (status == NV_ENC_SUCCESS) {
        currentBitrate_.store(bitrateBps, std::memory_order_relaxed);
        std::cout << "[NVENC] Dynamically reconfigured Bitrate to: " << (bitrateBps / 1000) << " kbps" << std::endl;
    } else {
        std::cerr << "[NVENC ERROR] nvEncReconfigureEncoder failed: " << status << std::endl;
    }
}

bool NVENCEncoder::EncodeSlot(int slotIndex, bool forceIDR) {
    if (!nvApi_ || !encoder_ || slotIndex < 0 || slotIndex >= kSlotCount) {
        return false;
    }

    auto& slot = slots_[slotIndex];

    if (context_ && slot.completionQuery) {
        context_->End(slot.completionQuery.Get());
        BOOL queryData = FALSE;
        while (context_->GetData(slot.completionQuery.Get(), &queryData, sizeof(BOOL), 0) == S_FALSE) {
        }
    }

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
        nvApi_->nvEncUnmapInputResource(encoder_, slot.mappedResource);
        slot.mappedResource = nullptr;
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

    nvApi_->nvEncUnmapInputResource(encoder_, slot.mappedResource);
    slot.mappedResource = nullptr;

    return true;
}

bool NVENCEncoder::EncodeCurrentSlot(bool forceIDR) {
    bool success = EncodeSlot(currentSlot_, forceIDR);
    if (success) {
        lastValidSlot_ = currentSlot_;
        currentSlot_ = (currentSlot_ + 1) % kSlotCount;
    }
    return success;
}

bool NVENCEncoder::EncodeLastValidSlot(bool forceIDR) {
    if (lastValidSlot_ < 0) {
        return false;
    }
    return EncodeSlot(lastValidSlot_, forceIDR);
}

bool NVENCEncoder::EncodeFrame(ID3D11Texture2D* texture, bool forceIDR) {
    if (!nvApi_ || !encoder_ || !texture || !context_) {
        return false;
    }

    auto& slot = slots_[currentSlot_];
    context_->CopyResource(slot.inputTexture.Get(), texture);

    return EncodeCurrentSlot(forceIDR);
}