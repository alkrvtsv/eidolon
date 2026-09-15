#include "renderer/d3d11_renderer.h"
#include <dxgi1_5.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

PerformanceHUD::PerformanceHUD() {
    lastTextUpdateTime_ = std::chrono::steady_clock::now();
    lastPeakResetTime_ = std::chrono::steady_clock::now();
    renderHistory_.fill(0.0f);
    presentHistory_.fill(0.0f);
}

PerformanceHUD::~PerformanceHUD() noexcept {
    Shutdown();
}

bool PerformanceHUD::Initialize(IDXGISwapChain* swapChain) {
    Shutdown();
    swapChain_ = swapChain;

    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        nullptr,
        reinterpret_cast<void**>(d2dFactory_.GetAddressOf())
    );
    if (FAILED(hr)) return false;

    D2D1_STROKE_STYLE_PROPERTIES strokeProps = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_FLAT,
        D2D1_CAP_STYLE_FLAT,
        D2D1_CAP_STYLE_FLAT,
        D2D1_LINE_JOIN_MITER,
        10.0f,
        D2D1_DASH_STYLE_DASH,
        0.0f
    );
    hr = d2dFactory_->CreateStrokeStyle(strokeProps, nullptr, 0, &dashedStrokeStyle_);
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())
    );
    if (FAILED(hr)) return false;

    hr = dwriteFactory_->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f,
        L"en-us",
        &textFormat_
    );
    if (FAILED(hr)) return false;

    hr = dwriteFactory_->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        10.0f,
        L"en-us",
        &graphLegendFormat_
    );
    if (FAILED(hr)) return false;

    return CreateDeviceResources();
}

void PerformanceHUD::Shutdown() noexcept {
    DiscardDeviceResources();
    dashedStrokeStyle_.Reset();
    graphLegendFormat_.Reset();
    textFormat_.Reset();
    dwriteFactory_.Reset();
    d2dFactory_.Reset();
    swapChain_ = nullptr;
}

void PerformanceHUD::DiscardDeviceResources() {
    presentLineBrush_.Reset();
    renderLineBrush_.Reset();
    gridBrush_.Reset();
    graphBgBrush_.Reset();
    backgroundBrush_.Reset();
    textBrush_.Reset();
    d2dRenderTarget_.Reset();
}

bool PerformanceHUD::CreateDeviceResources() {
    if (!swapChain_ || !d2dFactory_) return false;

    ComPtr<IDXGISurface> surface;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr)) return false;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    hr = d2dFactory_->CreateDxgiSurfaceRenderTarget(surface.Get(), &props, &d2dRenderTarget_);
    if (FAILED(hr)) return false;

    hr = d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.95f, 0.35f, 1.0f), &textBrush_);
    if (FAILED(hr)) return false;

    hr = d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.04f, 0.04f, 0.06f, 0.82f), &backgroundBrush_);
    if (FAILED(hr)) return false;

    hr = d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.02f, 0.02f, 0.03f, 0.90f), &graphBgBrush_);
    if (FAILED(hr)) return false;

    hr = d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.40f, 0.45f, 0.50f), &gridBrush_);
    if (FAILED(hr)) return false;

    hr = d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(1.00f, 0.60f, 0.15f, 1.0f), &renderLineBrush_);
    if (FAILED(hr)) return false;

    hr = d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.70f, 1.00f, 1.0f), &presentLineBrush_);
    return SUCCEEDED(hr);
}

void PerformanceHUD::Render(const PerformanceMetrics& metrics) {
    if (!visible_ || !d2dRenderTarget_ || !dwriteFactory_ || !textFormat_) return;

    renderHistory_[historyIndex_] = metrics.renderTimeMs;
    presentHistory_[historyIndex_] = metrics.presentTimeMs;
    historyIndex_ = (historyIndex_ + 1) % kGraphHistorySize;
    if (historyCount_ < kGraphHistorySize) {
        historyCount_++;
    }

    auto now = std::chrono::steady_clock::now();

    accumDecode_ += metrics.decodeTimeMs;
    accumRender_ += metrics.renderTimeMs;
    accumBlt_ += metrics.bltTimeMs;
    accumPresent_ += metrics.presentTimeMs;
    accumWait_ += metrics.waitLatencyMs;
    sampleCount_++;

    maxDecodeMs_ = (std::max)(maxDecodeMs_, metrics.decodeTimeMs);
    maxRenderMs_ = (std::max)(maxRenderMs_, metrics.renderTimeMs);
    maxBltMs_ = (std::max)(maxBltMs_, metrics.bltTimeMs);
    maxPresentMs_ = (std::max)(maxPresentMs_, metrics.presentTimeMs);
    maxWaitMs_ = (std::max)(maxWaitMs_, metrics.waitLatencyMs);

    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPeakResetTime_).count() >= 2000) {
        maxDecodeMs_ = metrics.decodeTimeMs;
        maxRenderMs_ = metrics.renderTimeMs;
        maxBltMs_ = metrics.bltTimeMs;
        maxPresentMs_ = metrics.presentTimeMs;
        maxWaitMs_ = metrics.waitLatencyMs;
        lastPeakResetTime_ = now;
    }

    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTextUpdateTime_).count() >= 250 || cachedText_.empty()) {
        float avgDecode = sampleCount_ > 0 ? (accumDecode_ / sampleCount_) : metrics.decodeTimeMs;
        float avgRender = sampleCount_ > 0 ? (accumRender_ / sampleCount_) : metrics.renderTimeMs;
        float avgBlt = sampleCount_ > 0 ? (accumBlt_ / sampleCount_) : metrics.bltTimeMs;
        float avgPresent = sampleCount_ > 0 ? (accumPresent_ / sampleCount_) : metrics.presentTimeMs;
        float avgWait = sampleCount_ > 0 ? (accumWait_ / sampleCount_) : metrics.waitLatencyMs;

        std::wstringstream ss;
        ss << std::fixed << std::setprecision(1);
        ss << L"Eidolon HUD [Ctrl+Shift+H]\n";
        ss << L"FPS: " << metrics.fps << L"\n";
        ss << L"Decode:  avg " << avgDecode << L" ms | max " << maxDecodeMs_ << L" ms\n";
        ss << L"Render:  avg " << avgRender << L" ms | max " << maxRenderMs_ << L" ms\n";
        ss << L"  Wait:  avg " << avgWait << L" ms | max " << maxWaitMs_ << L" ms\n";
        ss << L"  Blt:   avg " << avgBlt << L" ms | max " << maxBltMs_ << L" ms\n";
        ss << L"  Pres:  avg " << avgPresent << L" ms | max " << maxPresentMs_ << L" ms\n";
        ss << L"Video Queue: " << metrics.videoQueueSize << L"\n";
        ss << L"Audio Buffer: " << metrics.audioQueuedMs << L" ms\n";
        ss << L"Host: " << metrics.hostWidth << L"x" << metrics.hostHeight << L"\n";
        ss << L"Client: " << metrics.clientWidth << L"x" << metrics.clientHeight;

        cachedText_ = ss.str();
        accumDecode_ = 0.0f;
        accumRender_ = 0.0f;
        accumBlt_ = 0.0f;
        accumPresent_ = 0.0f;
        accumWait_ = 0.0f;
        sampleCount_ = 0;
        lastTextUpdateTime_ = now;
    }

    d2dRenderTarget_->BeginDraw();

    D2D1_RECT_F bgRect = D2D1::RectF(14.0f, 14.0f, 350.0f, 350.0f);
    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(bgRect, 6.0f, 6.0f);
    d2dRenderTarget_->FillRoundedRectangle(roundedRect, backgroundBrush_.Get());

    ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = dwriteFactory_->CreateTextLayout(
        cachedText_.c_str(),
        static_cast<UINT32>(cachedText_.length()),
        textFormat_.Get(),
        330.0f,
        210.0f,
        textLayout.GetAddressOf()
    );

    if (SUCCEEDED(hr) && textLayout) {
        D2D1_POINT_2F origin = D2D1::Point2F(24.0f, 22.0f);
        d2dRenderTarget_->DrawTextLayout(origin, textLayout.Get(), textBrush_.Get());
    }

    const float graphX = 24.0f;
    const float graphY = 236.0f;
    const float graphW = 312.0f;
    const float graphH = 80.0f;
    const float graphBottom = graphY + graphH;

    D2D1_RECT_F graphRect = D2D1::RectF(graphX, graphY, graphX + graphW, graphBottom);
    d2dRenderTarget_->FillRectangle(graphRect, graphBgBrush_.Get());
    d2dRenderTarget_->DrawRectangle(graphRect, gridBrush_.Get(), 1.0f);

    float maxPlotMs = 20.0f;
    for (size_t i = 0; i < historyCount_; ++i) {
        maxPlotMs = (std::max)({maxPlotMs, renderHistory_[i], presentHistory_[i]});
    }
    maxPlotMs = std::ceil(maxPlotMs / 5.0f) * 5.0f;

    auto timeToY = [&](float ms) -> float {
        float normalized = ms / maxPlotMs;
        normalized = (std::min)((std::max)(normalized, 0.0f), 1.0f);
        return graphBottom - (normalized * graphH);
    };

    float y8ms = timeToY(8.33f);
    d2dRenderTarget_->DrawLine(
        D2D1::Point2F(graphX, y8ms),
        D2D1::Point2F(graphX + graphW, y8ms),
        gridBrush_.Get(),
        0.8f,
        dashedStrokeStyle_.Get()
    );

    float y16ms = timeToY(16.66f);
    d2dRenderTarget_->DrawLine(
        D2D1::Point2F(graphX, y16ms),
        D2D1::Point2F(graphX + graphW, y16ms),
        gridBrush_.Get(),
        0.8f,
        dashedStrokeStyle_.Get()
    );

    if (historyCount_ > 1) {
        float stepX = graphW / static_cast<float>(kGraphHistorySize - 1);
        size_t startIdx = (historyCount_ < kGraphHistorySize) ? 0 : historyIndex_;

        for (size_t i = 1; i < historyCount_; ++i) {
            size_t prevSlot = (startIdx + i - 1) % kGraphHistorySize;
            size_t currSlot = (startIdx + i) % kGraphHistorySize;

            float x0 = graphX + static_cast<float>(i - 1) * stepX;
            float x1 = graphX + static_cast<float>(i) * stepX;

            float rendY0 = timeToY(renderHistory_[prevSlot]);
            float rendY1 = timeToY(renderHistory_[currSlot]);
            d2dRenderTarget_->DrawLine(
                D2D1::Point2F(x0, rendY0),
                D2D1::Point2F(x1, rendY1),
                renderLineBrush_.Get(),
                1.5f
            );

            float presY0 = timeToY(presentHistory_[prevSlot]);
            float presY1 = timeToY(presentHistory_[currSlot]);
            d2dRenderTarget_->DrawLine(
                D2D1::Point2F(x0, presY0),
                D2D1::Point2F(x1, presY1),
                presentLineBrush_.Get(),
                1.2f
            );
        }
    }

    const std::wstring legendText = L"Render (Orange) | Present (Blue) | 8.3ms / 16.6ms";
    d2dRenderTarget_->DrawText(
        legendText.c_str(),
        static_cast<UINT32>(legendText.length()),
        graphLegendFormat_.Get(),
        D2D1::RectF(graphX, graphBottom + 4.0f, graphX + graphW, graphBottom + 20.0f),
        gridBrush_.Get()
    );

    d2dRenderTarget_->EndDraw();
}

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

    hud_.Initialize(swapChain_.Get());
    return true;
}

void D3D11Renderer::Shutdown() noexcept {
    hud_.Shutdown();
    cachedInputViews_.clear();
    cachedInputTexture_ = nullptr;
    CleanupRenderTarget();
    outputView_.Reset();
    videoProcessor_.Reset();
    videoProcessorEnum_.Reset();
    videoContext_.Reset();
    videoDevice_.Reset();
    frameLatencyWaitableObject_ = nullptr;
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
}

bool D3D11Renderer::CreateDeviceAndSwapChain(HWND hwnd) {
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &device_,
        &featureLevel,
        &context_
    );
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice1> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) return false;

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;

    ComPtr<IDXGIFactory2> factory2;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory2)))) return false;

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = windowWidth_;
    scd.Height = windowHeight_;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.Stereo = FALSE;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 3;
    scd.Scaling = DXGI_SCALING_NONE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory2->CreateSwapChainForHwnd(
        device_.Get(),
        hwnd,
        &scd,
        nullptr,
        nullptr,
        &swapChain1
    );
    if (FAILED(hr)) return false;

    factory2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    ComPtr<IDXGISwapChain2> swapChain2;
    if (SUCCEEDED(swapChain1.As(&swapChain2))) {
        swapChain2->SetMaximumFrameLatency(1);
        frameLatencyWaitableObject_ = swapChain2->GetFrameLatencyWaitableObject();
    }

    hr = swapChain1.As(&swapChain_);
    if (FAILED(hr)) return false;

    device_.As(&videoDevice_);
    context_.As(&videoContext_);
    return true;
}

bool D3D11Renderer::CreateVideoProcessor() {
    if (!videoDevice_) return false;

    videoProcessor_.Reset();
    videoProcessorEnum_.Reset();
    cachedInputViews_.clear();
    cachedInputTexture_ = nullptr;

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

    D3D11_VIDEO_PROCESSOR_COLOR_SPACE inColorSpace = {};
    inColorSpace.Usage = 0;
    inColorSpace.RGB_Range = 0;
    inColorSpace.YCbCr_Matrix = 1;
    inColorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    videoContext_->VideoProcessorSetStreamColorSpace(videoProcessor_.Get(), 0, &inColorSpace);

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

    hud_.DiscardDeviceResources();
    CleanupRenderTarget();
    videoProcessor_.Reset();
    videoProcessorEnum_.Reset();
    cachedInputViews_.clear();
    cachedInputTexture_ = nullptr;

    swapChain_->ResizeBuffers(3, windowWidth_, windowHeight_, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);

    CreateVideoProcessor();
    CreateRenderTarget();
    hud_.CreateDeviceResources();
}

bool D3D11Renderer::WaitForFrameLatency(DWORD timeoutMs) {
    if (!frameLatencyWaitableObject_) return true;
    DWORD res = WaitForSingleObjectEx(frameLatencyWaitableObject_, timeoutMs, TRUE);
    return (res == WAIT_OBJECT_0);
}

void D3D11Renderer::RenderFrame(const DecodedFrame& frame, PerformanceMetrics& metrics) {
    if (!frame.texture || !swapChain_) return;

    if (frame.width > 0 && frame.height > 0 && (frame.width != videoWidth_ || frame.height != videoHeight_)) {
        videoWidth_ = frame.width;
        videoHeight_ = frame.height;
        hud_.DiscardDeviceResources();
        CleanupRenderTarget();
        CreateVideoProcessor();
        CreateRenderTarget();
        hud_.CreateDeviceResources();
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    if (videoContext_ && videoProcessor_ && outputView_ && videoProcessorEnum_) {
        if (cachedInputTexture_ != frame.texture) {
            cachedInputTexture_ = frame.texture;
            D3D11_TEXTURE2D_DESC texDesc = {};
            frame.texture->GetDesc(&texDesc);
            cachedInputViews_.assign(texDesc.ArraySize, nullptr);
        }

        if (frame.subresourceIndex >= cachedInputViews_.size()) {
            cachedInputViews_.resize(frame.subresourceIndex + 1);
        }

        if (!cachedInputViews_[frame.subresourceIndex]) {
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivd = {};
            ivd.FourCC = 0;
            ivd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            ivd.Texture2D.ArraySlice = frame.subresourceIndex;
            videoDevice_->CreateVideoProcessorInputView(
                frame.texture,
                videoProcessorEnum_.Get(),
                &ivd,
                &cachedInputViews_[frame.subresourceIndex]
            );
        }

        ID3D11VideoProcessorInputView* inputView = cachedInputViews_[frame.subresourceIndex].Get();
        if (inputView) {
            D3D11_VIDEO_PROCESSOR_STREAM stream = {};
            stream.Enable = TRUE;
            stream.pInputSurface = inputView;

            RECT srcRect = { 0, 0, static_cast<LONG>(frame.width), static_cast<LONG>(frame.height) };
            RECT dstRect = { 0, 0, static_cast<LONG>(windowWidth_), static_cast<LONG>(windowHeight_) };

            videoContext_->VideoProcessorSetStreamSourceRect(videoProcessor_.Get(), 0, TRUE, &srcRect);
            videoContext_->VideoProcessorSetStreamDestRect(videoProcessor_.Get(), 0, TRUE, &dstRect);

            videoContext_->VideoProcessorBlt(videoProcessor_.Get(), outputView_.Get(), 0, 1, &stream);
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    metrics.bltTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

    hud_.Render(metrics);

    auto p0 = std::chrono::high_resolution_clock::now();
    swapChain_->Present(0, 0);
    auto p1 = std::chrono::high_resolution_clock::now();
    metrics.presentTimeMs = std::chrono::duration<float, std::milli>(p1 - p0).count();
}