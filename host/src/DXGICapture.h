#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <iostream>

using Microsoft::WRL::ComPtr;

class DXGICapture {
public:
    DXGICapture();
    ~DXGICapture();

    bool Init();
    
    ID3D11Texture2D* AcquireFrame();

    void UnlockFrame();
    
    ID3D11Device* GetDevice() { return d3dDevice.Get(); }
    uint32_t GetWidth() const { return width; }
    uint32_t GetHeight() const { return height; }

private:
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGIOutputDuplication> deskDupl;

    uint32_t width = 0;
    uint32_t height = 0;

    bool HandleAccessLost();
};