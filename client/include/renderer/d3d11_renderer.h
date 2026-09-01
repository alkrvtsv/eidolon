#pragma once

#include "protocol.h"
#include "decoder/ffmpeg_d3d11va_decoder.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include <mutex>

using Microsoft::WRL::ComPtr;

class D3D11Renderer {
public:
    D3D11Renderer();
    ~D3D11Renderer() noexcept;

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void Shutdown() noexcept;
    void Resize(uint32_t width, uint32_t height);

    void RenderFrame(const DecodedFrame& frame);
    void UpdateCursorPosition(const CursorPositionMessage& pos);
    void UpdateCursorShape(const CursorShapeMessage& shape, const uint8_t* data);

    ID3D11Device* GetDevice() const { return device_.Get(); }
    ID3D11DeviceContext* GetContext() const { return context_.Get(); }

private:
    bool CreateDeviceAndSwapChain(HWND hwnd);
    bool CreateRenderTarget();
    void CleanupRenderTarget();
    bool CreateVideoProcessor();
    bool CreateCursorPipeline();
    bool UpdateCursorTexture(uint32_t width, uint32_t height, const uint8_t* rgbaData);

    HWND hwnd_{nullptr};
    uint32_t windowWidth_{1920};
    uint32_t windowHeight_{1080};
    uint32_t videoWidth_{0};
    uint32_t videoHeight_{0};

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swapChain_;
    ComPtr<ID3D11RenderTargetView> renderTargetView_;

    // D3D11 Video Processor
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<ID3D11VideoProcessorEnumerator> videoProcessorEnum_;
    ComPtr<ID3D11VideoProcessor> videoProcessor_;
    ComPtr<ID3D11VideoProcessorOutputView> outputView_;

    // Шейдерный пайплайн отрисовки курсора
    ComPtr<ID3D11VertexShader> cursorVS_;
    ComPtr<ID3D11PixelShader> cursorPS_;
    ComPtr<ID3D11Buffer> cursorCB_;
    ComPtr<ID3D11BlendState> blendState_;
    ComPtr<ID3D11SamplerState> samplerState_;

    // Состояние курсора
    std::mutex cursorMutex_;
    bool cursorVisible_{false};
    int32_t cursorX_{0};
    int32_t cursorY_{0};
    int32_t cursorHotspotX_{0};
    int32_t cursorHotspotY_{0};
    uint32_t cursorWidth_{0};
    uint32_t cursorHeight_{0};

    std::vector<uint8_t> cursorPixelData_;
    ComPtr<ID3D11Texture2D> cursorTexture_;
    ComPtr<ID3D11ShaderResourceView> cursorSRV_;
};