#pragma once

#include "renderer/video_renderer.h"
#include <d3d11_4.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

class D3D11Renderer final : public IVideoRenderer {
public:
    D3D11Renderer();
    ~D3D11Renderer() noexcept override;

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height) override;
    void Shutdown() noexcept override;

    void Resize(uint32_t width, uint32_t height) override;
    bool RenderFrame(const DecodedFrame& frame) override;

    void UpdateCursorShape(const CursorShapeMessage& shape, const uint8_t* data) override;
    void UpdateCursorPosition(const CursorPositionMessage& pos) override;

    ID3D11Device* GetDevice() const override { return device_.Get(); }
    ID3D11DeviceContext* GetContext() const override { return context_.Get(); }

private:
    bool CreateDevice();
    bool CreateSwapChain(HWND hwnd);
    bool CreateVideoProcessor(uint32_t inputWidth, uint32_t inputHeight);
    void UpdateRenderTargetViews();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swapChain_;
    
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<ID3D11VideoProcessorEnumerator> videoEnumerator_;
    ComPtr<ID3D11VideoProcessor> videoProcessor_;
    ComPtr<ID3D11VideoProcessorOutputView> vpOutputView_;

    ComPtr<ID3D11Texture2D> backBuffer_;
    ComPtr<ID3D11RenderTargetView> rtv_;

    uint32_t clientWidth_{0};
    uint32_t clientHeight_{0};
    uint32_t streamWidth_{0};
    uint32_t streamHeight_{0};
    bool tearingSupported_{false};

    CursorPositionMessage cursorPos_{};
    CursorShapeMessage cursorShape_{};
    std::vector<uint8_t> cursorData_;
    ComPtr<ID3D11Texture2D> cursorTexture_;
    ComPtr<ID3D11ShaderResourceView> cursorSrv_;
};