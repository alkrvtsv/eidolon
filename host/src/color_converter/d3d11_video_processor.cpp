#include "color_converter/d3d11_video_processor.h"

D3D11VideoProcessorConverter::D3D11VideoProcessorConverter() = default;

D3D11VideoProcessorConverter::~D3D11VideoProcessorConverter() {
    Shutdown();
}

bool D3D11VideoProcessorConverter::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width, uint32_t height) {
    if (!device || !context || width == 0 || height == 0) {
        return false;
    }

    Shutdown();

    device_ = device;
    context_ = context;
    width_ = width;
    height_ = height;

    HRESULT hr = device_.As(&videoDevice_);
    if (FAILED(hr)) {
        Shutdown();
        return false;
    }

    hr = context_.As(&videoContext_);
    if (FAILED(hr)) {
        Shutdown();
        return false;
    }

    if (!InitializeVideoPipeline()) {
        Shutdown();
        return false;
    }

    if (!CreateOutputResources()) {
        Shutdown();
        return false;
    }

    return true;
}

void D3D11VideoProcessorConverter::Shutdown() {
    outputView_.Reset();
    outputTextureNV12_.Reset();
    videoProcessor_.Reset();
    videoProcessorEnumerator_.Reset();
    videoContext_.Reset();
    videoDevice_.Reset();
    context_.Reset();
    device_.Reset();
    width_ = 0;
    height_ = 0;
}

bool D3D11VideoProcessorConverter::InitializeVideoPipeline() {
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputFrameRate.Numerator = 60;
    contentDesc.InputFrameRate.Denominator = 1;
    contentDesc.InputWidth = width_;
    contentDesc.InputHeight = height_;
    contentDesc.OutputWidth = width_;
    contentDesc.OutputHeight = height_;
    contentDesc.OutputFrameRate.Numerator = 60;
    contentDesc.OutputFrameRate.Denominator = 1;
    contentDesc.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;

    HRESULT hr = videoDevice_->CreateVideoProcessorEnumerator(&contentDesc, &videoProcessorEnumerator_);
    if (FAILED(hr)) {
        return false;
    }

    UINT flags = 0;
    hr = videoProcessorEnumerator_->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &flags);
    if (FAILED(hr) || !(flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
        return false;
    }

    hr = videoProcessorEnumerator_->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &flags);
    if (FAILED(hr) || !(flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
        return false;
    }

    hr = videoDevice_->CreateVideoProcessor(videoProcessorEnumerator_.Get(), 0, &videoProcessor_);
    if (FAILED(hr)) {
        return false;
    }

    // Входной поток: RGB Full Range (0..255)
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE inColorSpace = {};
    inColorSpace.Usage = 0;
    inColorSpace.RGB_Range = 0; // Full Range (0-255)
    inColorSpace.YCbCr_Matrix = 1; // BT.709
    inColorSpace.YCbCr_xvYCC = 0;
    inColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    videoContext_->VideoProcessorSetStreamColorSpace(videoProcessor_.Get(), 0, &inColorSpace);

    // Выходной поток: NV12 BT.709 Full Range
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE outColorSpace = inColorSpace;
    videoContext_->VideoProcessorSetOutputColorSpace(videoProcessor_.Get(), &outColorSpace);

    return true;
}

bool D3D11VideoProcessorConverter::CreateOutputResources() {
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width_;
    texDesc.Height = height_;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_NV12;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    HRESULT hr = device_->CreateTexture2D(&texDesc, nullptr, &outputTextureNV12_);
    if (FAILED(hr)) {
        return false;
    }

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outViewDesc = {};
    outViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outViewDesc.Texture2D.MipSlice = 0;

    hr = videoDevice_->CreateVideoProcessorOutputView(
        outputTextureNV12_.Get(),
        videoProcessorEnumerator_.Get(),
        &outViewDesc,
        &outputView_
    );

    return SUCCEEDED(hr);
}

bool D3D11VideoProcessorConverter::Convert(ID3D11Texture2D* pInputTexture, ID3D11Texture2D** ppOutputTexture) {
    if (!pInputTexture || !ppOutputTexture || !videoProcessor_ || !outputView_) {
        return false;
    }

    *ppOutputTexture = nullptr;

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inViewDesc = {};
    inViewDesc.FourCC = 0;
    inViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inViewDesc.Texture2D.MipSlice = 0;
    inViewDesc.Texture2D.ArraySlice = 0;

    ComPtr<ID3D11VideoProcessorInputView> inputView;
    HRESULT hr = videoDevice_->CreateVideoProcessorInputView(
        pInputTexture,
        videoProcessorEnumerator_.Get(),
        &inViewDesc,
        &inputView
    );

    if (FAILED(hr)) {
        return false;
    }

    D3D11_VIDEO_PROCESSOR_STREAM streamData = {};
    streamData.Enable = TRUE;
    streamData.OutputIndex = 0;
    streamData.InputFrameOrField = 0;
    streamData.PastFrames = 0;
    streamData.FutureFrames = 0;
    streamData.pInputSurface = inputView.Get();

    RECT srcRect = { 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
    RECT destRect = { 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };

    videoContext_->VideoProcessorSetStreamSourceRect(videoProcessor_.Get(), 0, TRUE, &srcRect);
    videoContext_->VideoProcessorSetStreamDestRect(videoProcessor_.Get(), 0, TRUE, &destRect);
    videoContext_->VideoProcessorSetOutputTargetRect(videoProcessor_.Get(), TRUE, &destRect);

    hr = videoContext_->VideoProcessorBlt(
        videoProcessor_.Get(),
        outputView_.Get(),
        0,
        1,
        &streamData
    );

    if (FAILED(hr)) {
        return false;
    }

    *ppOutputTexture = outputTextureNV12_.Get();
    (*ppOutputTexture)->AddRef();
    return true;
}