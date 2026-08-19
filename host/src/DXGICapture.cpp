#include "DXGICapture.h"
#include <iostream>
#include <thread>
#include <string>

#define NOMINMAX
#include <windows.h>
#include <dxgi1_2.h>

using Microsoft::WRL::ComPtr;

DXGICapture::DXGICapture() {}

DXGICapture::~DXGICapture() {
    if (deskDupl) deskDupl.Reset();
}

bool DXGICapture::Init() {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

    ComPtr<IDXGIOutputDuplication> newDupl;
    ComPtr<ID3D11Device> bestDevice;
    ComPtr<ID3D11DeviceContext> bestContext;
    UINT chosenAdapter = 0, chosenOutput = 0;

    std::cout << "[DXGI] Сканирование всех адаптеров и мониторов..." << std::endl;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);
            if (!desc.AttachedToDesktop) continue;

            ComPtr<ID3D11Device> tempDevice;
            ComPtr<ID3D11DeviceContext> tempContext;
            D3D_FEATURE_LEVEL featureLevel;
            D3D_FEATURE_LEVEL featureTypes[] = { D3D_FEATURE_LEVEL_11_0 };
            
            HRESULT hrD3D = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, 
                                              featureTypes, 1, D3D11_SDK_VERSION, 
                                              &tempDevice, &featureLevel, &tempContext);
            if (FAILED(hrD3D)) continue;

            MONITORINFOEXW mi = { sizeof(mi) };
            std::wstring deviceStr = L"";
            std::string devName = "Unknown";
            if (GetMonitorInfoW(desc.Monitor, &mi)) {
                DISPLAY_DEVICEW dd = { sizeof(dd) };
                if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0)) {
                    deviceStr = dd.DeviceString;
                    std::wstring ws(dd.DeviceString);
                    devName = std::string(ws.begin(), ws.end());
                }
            }

            bool isVirtual = (deviceStr.find(L"Indirect") != std::wstring::npos) ||
                             (deviceStr.find(L"Virtual") != std::wstring::npos) ||
                             (deviceStr.find(L"Idd") != std::wstring::npos) ||
                             (deviceStr.find(L"Parsec") != std::wstring::npos) ||
                             (deviceStr.find(L"VDD") != std::wstring::npos) ||
                             (deviceStr.find(L"MTT") != std::wstring::npos);

            std::cout << "  -> Адаптер [" << a << "] Монитор [" << o << "]: " << devName 
                      << (isVirtual ? " [ВИРТУАЛЬНЫЙ]" : "") << std::endl;

            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1))) continue;

            ComPtr<IDXGIOutputDuplication> duplTest;
            HRESULT hr = output1->DuplicateOutput(tempDevice.Get(), &duplTest);
            
            if (SUCCEEDED(hr)) {
                if (isVirtual) {
                    newDupl = duplTest;
                    bestDevice = tempDevice;
                    bestContext = tempContext;
                    chosenAdapter = a; chosenOutput = o;
                    width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
                    height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
                    std::cout << "[+] Приоритетно выбран ВИРТУАЛЬНЫЙ монитор!" << std::endl;
                    goto SearchDone; 
                }

                if (!newDupl) {
                    newDupl = duplTest;
                    bestDevice = tempDevice;
                    bestContext = tempContext;
                    chosenAdapter = a; chosenOutput = o;
                    width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
                    height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
                }
            } else {
                std::cerr << "     [x] Ошибка DuplicateOutput (код " << std::hex << hr << std::dec << ")" << std::endl;
            }
        }
    }

SearchDone:
    if (!newDupl) return false;

    deskDupl = newDupl;
    d3dDevice = bestDevice;
    d3dContext = bestContext;
    
    std::cout << "[+] DXGI успешно захватил Адаптер " << chosenAdapter << " Монитор " << chosenOutput 
              << " (" << width << "x" << height << ")" << std::endl;
    return true;
}

bool DXGICapture::HandleAccessLost() {
    if (deskDupl) deskDupl.Reset(); 
    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); 
    
    std::cout << "[-] Потерян доступ к монитору. Попытка восстановления..." << std::endl;
    if (Init()) {
        std::cout << "[+] Доступ к монитору успешно восстановлен!" << std::endl;
        return true;
    }
    return false;
}

ID3D11Texture2D* DXGICapture::AcquireFrame() {
    if (!deskDupl) {
        HandleAccessLost();
        return nullptr;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> desktopResource;
    HRESULT hr = deskDupl->AcquireNextFrame(500, &frameInfo, &desktopResource);
    
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        HandleAccessLost();
        return nullptr;
    }
    if (hr == DXGI_ERROR_WAIT_TIMEOUT || FAILED(hr)) return nullptr;

    ComPtr<ID3D11Texture2D> acquiredTexture;
    if (FAILED(desktopResource.As(&acquiredTexture))) {
        deskDupl->ReleaseFrame();
        return nullptr;
    }

    return acquiredTexture.Detach();
}

void DXGICapture::UnlockFrame() {
    if (deskDupl) deskDupl->ReleaseFrame();
}