#include "renderer/d3d11_renderer.h"
#include <iostream>

D3D11Renderer::D3D11Renderer() = default;

D3D11Renderer::~D3D11Renderer() noexcept {
    Shutdown();
}

bool D3D11Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height) {
    Shutdown();
    hwnd_ = hwnd;
    windowWidth_ = width;
    windowHeight_ = height;

    if (!CreateDeviceAndSwapChain(hwnd)) {
        return false;
    }
    if (!CreateVideoProcessor()) {
        return false;
    }
    if (!CreateRenderTarget()) {
        return false;
    }

    return true;
}

void D3D11Renderer::Shutdown() noexcept {
    CleanupRenderTarget();
    outputView_.Reset();
    videoProcessor_.Reset();
    videoProcessorEnum_.Reset();
    videoContext_.Reset();
    videoDevice_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
}

bool D3D11Renderer::CreateDeviceAndSwapChain(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = windowWidth_;
    scd.BufferDesc.Height = windowHeight_;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 0;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &scd,
        &swapChain_,
        &device_,
        &featureLevel,
        &context_
    );

    if (FAILED(hr)) return false;

    device_.As(&videoDevice_);
    context_.As(&videoContext_);
    return true;
}

bool D3D11Renderer::CreateVideoProcessor() {
    if (!videoDevice_) return false;

    videoProcessor_.Reset();
    videoProcessorEnum_.Reset();

    uint32_t inW = (videoWidth_ > 0) ? videoWidth_ : windowWidth_;
    uint32_t inH = (videoHeight_ > 0) ? videoHeight_ : windowHeight_;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = {};
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputWidth = inW;
    desc.InputHeight = inH;
    desc.OutputWidth = windowWidth_;
    desc.OutputHeight = windowHeight_;
    desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    HRESULT hr = videoDevice_->CreateVideoProcessorEnumerator(&desc, &videoProcessorEnum_);
    if (FAILED(hr)) return false;

    hr = videoDevice_->CreateVideoProcessor(videoProcessorEnum_.Get(), 0, &videoProcessor_);
    if (FAILED(hr)) return false;

    // Входной поток декодера: NV12 BT.709
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE inColorSpace = {};
    inColorSpace.Usage = 0;
    inColorSpace.RGB_Range = 0;
    inColorSpace.YCbCr_Matrix = 1; // BT.709
    inColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    videoContext_->VideoProcessorSetStreamColorSpace(videoProcessor_.Get(), 0, &inColorSpace);

    // Выходной поток SwapChain: RGB Full Range (0-255)
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE outColorSpace = {};
    outColorSpace.Usage = 0;
    outColorSpace.RGB_Range = 0;
    outColorSpace.YCbCr_Matrix = 0;
    outColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    videoContext_->VideoProcessorSetOutputColorSpace(videoProcessor_.Get(), &outColorSpace);

    return true;
}

bool D3D11Renderer::CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    hr = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView_);
    if (FAILED(hr)) return false;

    if (videoDevice_ && videoProcessorEnum_) {
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovd = {};
        ovd.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        hr = videoDevice_->CreateVideoProcessorOutputView(backBuffer.Get(), videoProcessorEnum_.Get(), &ovd, &outputView_);
    }

    return SUCCEEDED(hr);
}

void D3D11Renderer::CleanupRenderTarget() {
    outputView_.Reset();
    renderTargetView_.Reset();
}

void D3D11Renderer::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (width == windowWidth_ && height == windowHeight_)) return;

    windowWidth_ = width;
    windowHeight_ = height;

    CleanupRenderTarget();
    videoProcessor_.Reset();
    videoProcessorEnum_.Reset();

    swapChain_->ResizeBuffers(0, windowWidth_, windowHeight_, DXGI_FORMAT_UNKNOWN, 0);

    CreateVideoProcessor();
    CreateRenderTarget();
}

void D3D11Renderer::RenderFrame(const DecodedFrame& frame) {
    if (!frame.texture || !swapChain_) return;

    if (frame.width > 0 && frame.height > 0 && (frame.width != videoWidth_ || frame.height != videoHeight_)) {
        videoWidth_ = frame.width;
        videoHeight_ = frame.height;
        CleanupRenderTarget();
        CreateVideoProcessor();
        CreateRenderTarget();
    }

    if (videoContext_ && videoProcessor_ && outputView_ && videoProcessorEnum_) {
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivd = {};
        ivd.FourCC = 0;
        ivd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        ivd.Texture2D.ArraySlice = frame.subresourceIndex;

        ComPtr<ID3D11VideoProcessorInputView> inputView;
        HRESULT hr = videoDevice_->CreateVideoProcessorInputView(frame.texture, videoProcessorEnum_.Get(), &ivd, &inputView);

        if (SUCCEEDED(hr)) {
            D3D11_VIDEO_PROCESSOR_STREAM stream = {};
            stream.Enable = TRUE;
            stream.pInputSurface = inputView.Get();

            RECT srcRect = { 0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height) };
            RECT dstRect = { 0, 0, static_cast<LONG>(windowWidth_), static_cast<LONG>(windowHeight_) };

            videoContext_->VideoProcessorSetStreamSourceRect(videoProcessor_.Get(), 0, TRUE, &srcRect);
            videoContext_->VideoProcessorSetStreamDestRect(videoProcessor_.Get(), 0, TRUE, &dstRect);

            videoContext_->VideoProcessorBlt(videoProcessor_.Get(), outputView_.Get(), 0, 1, &stream);
        }
    }

    swapChain_->Present(0, 0);
}