#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

int main() {
    // Устанавливаем кодировку консоли
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "==========================================\n";
    std::cout << "    DXGI Monitor Enumeration Tool\n";
    std::cout << "==========================================\n\n";

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        std::cerr << "[-] Ошибка: Не удалось создать IDXGIFactory1. HRESULT: 0x" << std::hex << hr << std::endl;
        return -1;
    }

    ComPtr<IDXGIAdapter1> adapter;
    UINT adapterIndex = 0;
    bool foundAnyMonitor = false;

    // Перебираем видеокарты
    while (factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 adapterDesc;
        adapter->GetDesc1(&adapterDesc);
        
        std::string adapterName = WStringToString(adapterDesc.Description);
        std::cout << "[Adapter " << adapterIndex << "] " << adapterName << "\n";
        
        double vramMB = (double)adapterDesc.DedicatedVideoMemory / (1024 * 1024);
        std::cout << "  VRAM: " << vramMB << " MB\n";

        ComPtr<ID3D11Device> d3dDevice;
        D3D_FEATURE_LEVEL featureLevel;
        D3D_FEATURE_LEVEL featureTypes[] = { D3D_FEATURE_LEVEL_11_0 };
        HRESULT hrD3D = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, 
                                          featureTypes, 1, D3D11_SDK_VERSION, 
                                          &d3dDevice, &featureLevel, nullptr);
        
        if (FAILED(hrD3D)) {
             std::cout << "  -> (Не удалось создать D3D11 Device на этом адаптере. Пропускаем экраны)\n\n";
             adapterIndex++;
             continue;
        }

        ComPtr<IDXGIOutput> output;
        UINT outputIndex = 0;
        bool adapterHasMonitors = false;

        // Перебираем мониторы
        while (adapter->EnumOutputs(outputIndex, &output) != DXGI_ERROR_NOT_FOUND) {
            adapterHasMonitors = true;
            foundAnyMonitor = true;
            
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);

            std::string outputName = WStringToString(desc.DeviceName);
            int width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
            int height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
            
            std::cout << "    [Monitor " << outputIndex << "] " << outputName << "\n";
            std::cout << "      State:       " << (desc.AttachedToDesktop ? "Attached to Desktop (Active)" : "Disconnected") << "\n";
            std::cout << "      Resolution:  " << width << "x" << height << "\n";

            MONITORINFOEXW mi = { sizeof(mi) };
            if (GetMonitorInfoW(desc.Monitor, &mi)) {
                DISPLAY_DEVICEW dd = { sizeof(dd) };
                if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0)) {
                    std::string deviceString = WStringToString(dd.DeviceString);
                    std::cout << "      Driver Name: " << deviceString << "\n";
                }
            }

            if (desc.AttachedToDesktop) {
                ComPtr<IDXGIOutput1> output1;
                if (SUCCEEDED(output.As(&output1))) {
                    ComPtr<IDXGIOutputDuplication> duplTest;
                    HRESULT dupHr = output1->DuplicateOutput(d3dDevice.Get(), &duplTest);
                    if (SUCCEEDED(dupHr)) {
                        std::cout << "      Duplication: SUCCESS (Ready for capture)\n";
                    } else {
                        std::cout << "      Duplication: FAILED (HRESULT: 0x" << std::hex << dupHr << std::dec << ")\n";
                        if (dupHr == 0x80070057) std::cout << "                   -> E_INVALIDARG (Usually RDP block or wrong GPU)\n";
                        if (dupHr == 0x80070005) std::cout << "                   -> E_ACCESSDENIED (Screen locked or UAC prompt)\n";
                    }
                }
            }
            std::cout << "\n";
            outputIndex++;
        }
        
        if (!adapterHasMonitors) {
            std::cout << "    -> No monitors attached.\n\n";
        }

        adapterIndex++;
    }

    if (!foundAnyMonitor) {
        std::cout << "[-] Ни одного монитора не найдено в системе!\n";
    }

    std::cout << "==========================================\n";
    std::cout << "Press Enter to exit...\n";
    std::cin.get();

    return 0;
}