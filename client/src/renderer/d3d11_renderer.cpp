#include "renderer/d3d11_renderer.h"
#include <d3dcompiler.h>
#include <iostream>

#pragma comment(lib, "d3dcompiler.lib")

struct CursorCBData {
    float rect[4];     // x, y, width, height в экранных пикселях
    float viewport[2]; // windowWidth, windowHeight
    float pad[2];
};

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
    if (!CreateCursorPipeline()) {
        return false;
    }

    return true;
}

void D3D11Renderer::Shutdown() noexcept {
    CleanupRenderTarget();
    blendState_.Reset();
    samplerState_.Reset();
    cursorCB_.Reset();
    cursorPS_.Reset();
    cursorVS_.Reset();
    outputView_.Reset();
    videoProcessor_.Reset();
    videoProcessorEnum_.Reset();
    videoContext_.Reset();
    videoDevice_.Reset();
    cursorSRV_.Reset();
    cursorTexture_.Reset();
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

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = {};
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputWidth = windowWidth_;
    desc.InputHeight = windowHeight_;
    desc.OutputWidth = windowWidth_;
    desc.OutputHeight = windowHeight_;
    desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    HRESULT hr = videoDevice_->CreateVideoProcessorEnumerator(&desc, &videoProcessorEnum_);
    if (FAILED(hr)) return false;

    hr = videoDevice_->CreateVideoProcessor(videoProcessorEnum_.Get(), 0, &videoProcessor_);
    return SUCCEEDED(hr);
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

bool D3D11Renderer::CreateCursorPipeline() {
    const char* vsSource = R"(
        cbuffer CursorCB : register(b0) {
            float4 rect;
            float2 viewport;
            float2 pad;
        };
        struct VS_OUT {
            float4 pos : SV_POSITION;
            float2 uv : TEXCOORD0;
        };
        VS_OUT main(uint id : SV_VertexID) {
            VS_OUT output;
            float2 uv = float2((id == 1 || id == 4 || id == 5) ? 1.0 : 0.0,
                               (id == 2 || id == 3 || id == 5) ? 1.0 : 0.0);
            float2 pixelPos = rect.xy + uv * rect.zw;
            float2 ndc = float2((pixelPos.x / viewport.x) * 2.0 - 1.0,
                                1.0 - (pixelPos.y / viewport.y) * 2.0);
            output.pos = float4(ndc, 0.0, 1.0);
            output.uv = uv;
            return output;
        }
    )";

    const char* psSource = R"(
        Texture2D tex : register(t0);
        SamplerState samp : register(s0);
        struct VS_OUT {
            float4 pos : SV_POSITION;
            float2 uv : TEXCOORD0;
        };
        float4 main(VS_OUT input) : SV_TARGET {
            return tex.Sample(samp, input.uv);
        }
    )";

    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;
    HRESULT hr = D3DCompile(vsSource, strlen(vsSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) return false;
    hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &cursorVS_);
    if (FAILED(hr)) return false;

    hr = D3DCompile(psSource, strlen(psSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) return false;
    hr = device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &cursorPS_);
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(CursorCBData);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device_->CreateBuffer(&cbDesc, nullptr, &cursorCB_);
    if (FAILED(hr)) return false;

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device_->CreateBlendState(&blendDesc, &blendState_);
    if (FAILED(hr)) return false;

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    hr = device_->CreateSamplerState(&sampDesc, &samplerState_);
    return SUCCEEDED(hr);
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

    videoWidth_ = frame.width;
    videoHeight_ = frame.height;

    // 1. Аппаратный блит кадра видео в SwapChain
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

    // 2. Отрисовка аппаратного оверлея курсора
    {
        std::lock_guard<std::mutex> lock(cursorMutex_);
        if (cursorVisible_ && cursorSRV_ && cursorWidth_ > 0 && cursorHeight_ > 0) {
            ID3D11RenderTargetView* rtv = renderTargetView_.Get();
            context_->OMSetRenderTargets(1, &rtv, nullptr);

            D3D11_VIEWPORT vp = {};
            vp.Width = static_cast<float>(windowWidth_);
            vp.Height = static_cast<float>(windowHeight_);
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            context_->RSSetViewports(1, &vp);

            float scaleX = (videoWidth_ > 0) ? (static_cast<float>(windowWidth_) / videoWidth_) : 1.0f;
            float scaleY = (videoHeight_ > 0) ? (static_cast<float>(windowHeight_) / videoHeight_) : 1.0f;

            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(context_->Map(cursorCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                auto* cb = static_cast<CursorCBData*>(mapped.pData);
                cb->rect[0] = static_cast<float>(cursorX_) - (cursorHotspotX_ * scaleX);
                cb->rect[1] = static_cast<float>(cursorY_) - (cursorHotspotY_ * scaleY);
                cb->rect[2] = cursorWidth_ * scaleX;
                cb->rect[3] = cursorHeight_ * scaleY;
                cb->viewport[0] = static_cast<float>(windowWidth_);
                cb->viewport[1] = static_cast<float>(windowHeight_);
                context_->Unmap(cursorCB_.Get(), 0);
            }

            context_->VSSetShader(cursorVS_.Get(), nullptr, 0);
            context_->VSSetConstantBuffers(0, 1, cursorCB_.GetAddressOf());
            context_->PSSetShader(cursorPS_.Get(), nullptr, 0);
            context_->PSSetShaderResources(0, 1, cursorSRV_.GetAddressOf());
            context_->PSSetSamplers(0, 1, samplerState_.GetAddressOf());

            float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
            context_->OMSetBlendState(blendState_.Get(), blendFactor, 0xFFFFFFFF);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            context_->Draw(6, 0);
        }
    }

    swapChain_->Present(0, 0);
}

void D3D11Renderer::UpdateCursorPosition(const CursorPositionMessage& pos) {
    std::lock_guard<std::mutex> lock(cursorMutex_);
    cursorVisible_ = (pos.visible != 0);

    if (videoWidth_ > 0 && videoHeight_ > 0) {
        cursorX_ = (pos.x * static_cast<int32_t>(windowWidth_)) / static_cast<int32_t>(videoWidth_);
        cursorY_ = (pos.y * static_cast<int32_t>(windowHeight_)) / static_cast<int32_t>(videoHeight_);
    } else {
        cursorX_ = pos.x;
        cursorY_ = pos.y;
    }
}

void D3D11Renderer::UpdateCursorShape(const CursorShapeMessage& shape, const uint8_t* data) {
    std::lock_guard<std::mutex> lock(cursorMutex_);
    cursorWidth_ = shape.width;
    cursorHeight_ = shape.height;
    cursorHotspotX_ = shape.hotspotX;
    cursorHotspotY_ = shape.hotspotY;

    if (data && shape.dataSize > 0) {
        UpdateCursorTexture(shape.width, shape.height, data);
    }
}

bool D3D11Renderer::UpdateCursorTexture(uint32_t width, uint32_t height, const uint8_t* rgbaData) {
    if (!device_ || width == 0 || height == 0 || !rgbaData) return false;

    cursorSRV_.Reset();
    cursorTexture_.Reset();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgbaData;
    initData.SysMemPitch = width * 4;

    HRESULT hr = device_->CreateTexture2D(&desc, &initData, &cursorTexture_);
    if (FAILED(hr)) return false;

    hr = device_->CreateShaderResourceView(cursorTexture_.Get(), nullptr, &cursorSRV_);
    return SUCCEEDED(hr);
}