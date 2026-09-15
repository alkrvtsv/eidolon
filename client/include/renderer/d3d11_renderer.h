#pragma once

#include "protocol.h"
#include "decoder/ffmpeg_d3d11va_decoder.h"
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dwrite.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <chrono>
#include <vector>
#include <array>

using Microsoft::WRL::ComPtr;

struct PerformanceMetrics {
    float fps{0.0f};
    float decodeTimeMs{0.0f};
    float renderTimeMs{0.0f};
    float bltTimeMs{0.0f};
    float presentTimeMs{0.0f};
    size_t videoQueueSize{0};
    uint32_t audioQueuedMs{0};
    uint32_t hostWidth{0};
    uint32_t hostHeight{0};
    uint32_t clientWidth{0};
    uint32_t clientHeight{0};
};

class PerformanceHUD {
public:
    PerformanceHUD();
    ~PerformanceHUD() noexcept;

    bool Initialize(IDXGISwapChain* swapChain);
    void Shutdown() noexcept;

    bool CreateDeviceResources();
    void DiscardDeviceResources();

    void Render(const PerformanceMetrics& metrics);
    void Toggle() { visible_ = !visible_; }
    bool IsVisible() const { return visible_; }
    std::wstring GetFormattedText() const { return cachedText_; }

private:
    static constexpr size_t kGraphHistorySize = 160;

    IDXGISwapChain* swapChain_{nullptr};
    bool visible_{true};

    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<IDWriteTextFormat> textFormat_;
    ComPtr<IDWriteTextFormat> graphLegendFormat_;
    ComPtr<ID2D1RenderTarget> d2dRenderTarget_;
    ComPtr<ID2D1SolidColorBrush> textBrush_;
    ComPtr<ID2D1SolidColorBrush> backgroundBrush_;
    ComPtr<ID2D1SolidColorBrush> graphBgBrush_;
    ComPtr<ID2D1SolidColorBrush> gridBrush_;
    ComPtr<ID2D1SolidColorBrush> renderLineBrush_;
    ComPtr<ID2D1SolidColorBrush> presentLineBrush_;
    ComPtr<ID2D1StrokeStyle> dashedStrokeStyle_;

    std::wstring cachedText_;
    std::chrono::steady_clock::time_point lastTextUpdateTime_;
    std::chrono::steady_clock::time_point lastPeakResetTime_;
    float maxDecodeMs_{0.0f};
    float maxRenderMs_{0.0f};
    float maxBltMs_{0.0f};
    float maxPresentMs_{0.0f};
    float accumDecode_{0.0f};
    float accumRender_{0.0f};
    float accumBlt_{0.0f};
    float accumPresent_{0.0f};
    uint32_t sampleCount_{0};

    std::array<float, kGraphHistorySize> renderHistory_{};
    std::array<float, kGraphHistorySize> presentHistory_{};
    size_t historyIndex_{0};
    size_t historyCount_{0};
};

class D3D11Renderer {
public:
    D3D11Renderer();
    ~D3D11Renderer() noexcept;

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void Shutdown() noexcept;
    void Resize(uint32_t width, uint32_t height);

    void RenderFrame(const DecodedFrame& frame, PerformanceMetrics& metrics);
    void ToggleHUD() { hud_.Toggle(); }
    std::wstring GetHUDText() const { return hud_.GetFormattedText(); }

    ID3D11Device* GetDevice() const { return device_.Get(); }
    ID3D11DeviceContext* GetContext() const { return context_.Get(); }

private:
    bool CreateDeviceAndSwapChain(HWND hwnd);
    bool CreateRenderTarget();
    void CleanupRenderTarget();
    bool CreateVideoProcessor();

    HWND hwnd_{nullptr};
    uint32_t windowWidth_{1920};
    uint32_t windowHeight_{1080};
    uint32_t videoWidth_{0};
    uint32_t videoHeight_{0};

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swapChain_;
    ComPtr<ID3D11RenderTargetView> renderTargetView_;

    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<ID3D11VideoProcessorEnumerator> videoProcessorEnum_;
    ComPtr<ID3D11VideoProcessor> videoProcessor_;
    ComPtr<ID3D11VideoProcessorOutputView> outputView_;

    ID3D11Texture2D* cachedInputTexture_{nullptr};
    std::vector<ComPtr<ID3D11VideoProcessorInputView>> cachedInputViews_;

    PerformanceHUD hud_;
};