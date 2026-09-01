#include "renderer/d3d11_renderer.h"
#include <iostream>

D3D11Renderer::D3D11Renderer() = default;

D3D11Renderer::~D3D11Renderer() noexcept {
    Shutdown();
}

bool D3D11Renderer::CreateDevice() {
    UINT flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    D3D_FEATURE_LEVEL chosenLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        static_cast<UINT>(std::size(featureLevels)),
        D3D11_SDK_VERSION,
        &device_,
        &chosenLevel,
        &context_
    );

    if (FAILED(hr)) {
        std::cerr << "[Renderer] D3D11CreateDevice failed: " << std::hex << hr << std::endl;
        return false;
    }

    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(context_.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
    }

    hr = device_.As(&videoDevice_);
    if (FAILED(hr)) {
        return false;
    }

    hr = context_.As(&videoContext_);
    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreateSwapChain(HWND hwnd) {
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) {
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) {
        return false;
    }

    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        return false;
    }

    BOOL allowTearing = FALSE;
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory.As(&factory5))) {
        factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
    }
    tearingSupported_ = (allowTearing == TRUE);

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = clientWidth_;
    desc.Height = clientHeight_;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc.Flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    HRESULT hr = factory->CreateSwapChainForHwnd(
        device_.Get(),
        hwnd,
        &desc,
        nullptr,
        nullptr,
        &swapChain_
    );

    if (FAILED(hr)) {
        std::cerr << "[Renderer] CreateSwapChainForHwnd failed: " << std::hex << hr << std::endl;
        return false;
    }

    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    UpdateRenderTargetViews();

    return true;
}

void D3D11Renderer::UpdateRenderTargetViews() {
    rtv_.Reset();
    backBuffer_.Reset();
    vpOutputView_.Reset();

    if (!swapChain_) {
        return;
    }

    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer_));
    if (FAILED(hr)) {
        return;
    }

    device_->CreateRenderTargetView(backBuffer_.Get(), nullptr, &rtv_);

    if (videoDevice_ && videoEnumerator_) {
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outViewDesc = {};
        outViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        outViewDesc.Texture2D.MipSlice = 0;

        videoDevice_->CreateVideoProcessorOutputView(
            backBuffer_.Get(),
            videoEnumerator_.Get(),
            &outViewDesc,
            &vpOutputView_
        );
    }
}

bool D3D11Renderer::CreateVideoProcessor(uint32_t inputWidth, uint32_t inputHeight) {
    if (streamWidth_ == inputWidth && streamHeight_ == inputHeight && videoProcessor_ && vpOutputView_) {
        return true;
    }

    videoProcessor_.Reset();
    videoEnumerator_.Reset();
    vpOutputView_.Reset();

    streamWidth_ = inputWidth;
    streamHeight_ = inputHeight;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputFrameRate.Numerator = 60;
    contentDesc.InputFrameRate.Denominator = 1;
    contentDesc.InputWidth = streamWidth_;
    contentDesc.InputHeight = streamHeight_;
    contentDesc.OutputWidth = clientWidth_;
    contentDesc.OutputHeight = clientHeight_;
    contentDesc.OutputFrameRate.Numerator = 60;
    contentDesc.OutputFrameRate.Denominator = 1;
    contentDesc.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;

    HRESULT hr = videoDevice_->CreateVideoProcessorEnumerator(&contentDesc, &videoEnumerator_);
    if (FAILED(hr)) {
        std::cerr << "[Renderer] CreateVideoProcessorEnumerator failed: " << std::hex << hr << std::endl;
        return false;
    }

    hr = videoDevice_->CreateVideoProcessor(videoEnumerator_.Get(), 0, &videoProcessor_);
    if (FAILED(hr)) {
        std::cerr << "[Renderer] CreateVideoProcessor failed: " << std::hex << hr << std::endl;
        return false;
    }

    D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace = {};
    colorSpace.Usage = 0;
    colorSpace.RGB_Range = 0;
    colorSpace.YCbCr_Matrix = 1;
    colorSpace.YCbCr_xvYCC = 0;

    videoContext_->VideoProcessorSetStreamColorSpace(videoProcessor_.Get(), 0, &colorSpace);
    videoContext_->VideoProcessorSetOutputColorSpace(videoProcessor_.Get(), &colorSpace);

    UpdateRenderTargetViews();
    return true;
}

bool D3D11Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height) {
    Shutdown();

    clientWidth_ = width;
    clientHeight_ = height;

    if (!CreateDevice()) {
        Shutdown();
        return false;
    }

    if (!CreateSwapChain(hwnd)) {
        Shutdown();
        return false;
    }

    return true;
}

void D3D11Renderer::Shutdown() noexcept {
    cursorSrv_.Reset();
    cursorTexture_.Reset();
    cursorData_.clear();
    rtv_.Reset();
    backBuffer_.Reset();
    vpOutputView_.Reset();
    videoProcessor_.Reset();
    videoEnumerator_.Reset();
    videoContext_.Reset();
    videoDevice_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
    clientWidth_ = 0;
    clientHeight_ = 0;
    streamWidth_ = 0;
    streamHeight_ = 0;
}

void D3D11Renderer::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || !swapChain_) {
        return;
    }

    clientWidth_ = width;
    clientHeight_ = height;

    rtv_.Reset();
    backBuffer_.Reset();
    vpOutputView_.Reset();

    UINT flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    swapChain_->ResizeBuffers(0, clientWidth_, clientHeight_, DXGI_FORMAT_UNKNOWN, flags);

    if (streamWidth_ > 0 && streamHeight_ > 0) {
        CreateVideoProcessor(streamWidth_, streamHeight_);
    } else {
        UpdateRenderTargetViews();
    }
}

bool D3D11Renderer::RenderFrame(const DecodedFrame& frame) {
    if (!frame.texture || !swapChain_) {
        return false;
    }

    if (!CreateVideoProcessor(frame.width, frame.height)) {
        return false;
    }

    if (!vpOutputView_) {
        return false;
    }

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inViewDesc = {};
    inViewDesc.FourCC = 0;
    inViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inViewDesc.Texture2D.MipSlice = 0;
    inViewDesc.Texture2D.ArraySlice = frame.subresourceIndex;

    ComPtr<ID3D11VideoProcessorInputView> inputView;
    HRESULT hr = videoDevice_->CreateVideoProcessorInputView(
        frame.texture,
        videoEnumerator_.Get(),
        &inViewDesc,
        &inputView
    );

    if (FAILED(hr)) {
        std::cerr << "[Renderer] CreateVideoProcessorInputView failed: " << std::hex << hr << std::endl;
        return false;
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream = {};
    stream.Enable = TRUE;
    stream.OutputIndex = 0;
    stream.InputFrameOrField = 0;
    stream.PastFrames = 0;
    stream.FutureFrames = 0;
    stream.pInputSurface = inputView.Get();

    RECT srcRect = { 0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height) };
    RECT destRect = { 0, 0, static_cast<LONG>(clientWidth_), static_cast<LONG>(clientHeight_) };

    videoContext_->VideoProcessorSetStreamSourceRect(videoProcessor_.Get(), 0, TRUE, &srcRect);
    videoContext_->VideoProcessorSetStreamDestRect(videoProcessor_.Get(), 0, TRUE, &destRect);
    videoContext_->VideoProcessorSetOutputTargetRect(videoProcessor_.Get(), TRUE, &destRect);

    hr = videoContext_->VideoProcessorBlt(
        videoProcessor_.Get(),
        vpOutputView_.Get(),
        0,
        1,
        &stream
    );

    if (FAILED(hr)) {
        std::cerr << "[Renderer] VideoProcessorBlt failed: " << std::hex << hr << std::endl;
        return false;
    }

    UINT presentFlags = tearingSupported_ ? DXGI_PRESENT_ALLOW_TEARING : 0;
    hr = swapChain_->Present(0, presentFlags);

    return SUCCEEDED(hr);
}

void D3D11Renderer::UpdateCursorShape(const CursorShapeMessage& shape, const uint8_t* data) {
    cursorShape_ = shape;
    if (data && shape.dataSize > 0) {
        cursorData_.assign(data, data + shape.dataSize);
    }
}

void D3D11Renderer::UpdateCursorPosition(const CursorPositionMessage& pos) {
    cursorPos_ = pos;
}