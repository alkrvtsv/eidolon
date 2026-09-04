#pragma once

#include "color_converter/color_converter.h"
#include <d3d11_1.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class D3D11VideoProcessorConverter final : public IColorConverter {
public:
    D3D11VideoProcessorConverter();
    ~D3D11VideoProcessorConverter() override;

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width, uint32_t height) override;
    void Shutdown() override;

    bool Convert(ID3D11Texture2D* pInputTexture, ID3D11Texture2D** ppOutputTexture) override;

    uint32_t GetWidth() const override { return width_; }
    uint32_t GetHeight() const override { return height_; }

private:
    bool CreateOutputResources();
    bool InitializeVideoPipeline();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<ID3D11VideoProcessorEnumerator> videoProcessorEnumerator_;
    ComPtr<ID3D11VideoProcessor> videoProcessor_;

    ComPtr<ID3D11Texture2D> outputTextureNV12_;
    ComPtr<ID3D11VideoProcessorOutputView> outputView_;

    uint32_t width_{0};
    uint32_t height_{0};
};