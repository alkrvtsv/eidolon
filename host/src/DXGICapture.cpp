#include "DXGICapture.h"

DXGICapture::DXGICapture() {}
DXGICapture::~DXGICapture() {}

bool DXGICapture::Init() {
    HRESULT hr;

    // 1. Создаем DXGI Factory для перебора видеокарт
    ComPtr<IDXGIFactory1> factory;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
    if (FAILED(hr)) {
        std::cerr << "[-] Ошибка создания DXGIFactory1." << std::endl;
        return false;
    }

    ComPtr<IDXGIAdapter1> selectedAdapter;
    ComPtr<IDXGIAdapter1> adapter;
    
    // Перебираем все видеокарты в системе и ищем NVIDIA (Vendor ID = 0x10DE)
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        
        // Vendor ID NVIDIA равен 0x10DE
        if (desc.VendorId == 0x10DE) {
            selectedAdapter = adapter;
            std::wcout << L"[+] Выбрана видеокарта: " << desc.Description << std::endl;
            break;
        }
    }

    // Если NVIDIA не найдена явным образом, берем первый доступный адаптер
    if (!selectedAdapter) {
        factory->EnumAdapters1(0, &selectedAdapter);
        std::cout << "[!] Видеокарта NVIDIA не найдена по VendorID, используется адаптер по умолчанию." << std::endl;
    }

    // 2. Создаем устройство DirectX 11 на выбранной видеокарте
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext
    );

    if (FAILED(hr)) {
        std::cerr << "[-] Ошибка: Не удалось создать D3D11 Device." << std::endl;
        return false;
    }

    // 3. Получаем монитор (EnumOutputs)
    ComPtr<IDXGIOutput> dxgiOutput;
    hr = selectedAdapter->EnumOutputs(0, &dxgiOutput);
    if (FAILED(hr)) {
        std::cerr << "[-] Ошибка: Монитор не найден на выбранном адаптере." << std::endl;
        return false;
    }

    // Узнаем честное разрешение выбранного монитора
    DXGI_OUTPUT_DESC outputDesc;
    dxgiOutput->GetDesc(&outputDesc);
    width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
    
    std::cout << "[+] Разрешение захвата: " << width << "x" << height << std::endl;

    // 4. Создаем интерфейс дубликации экрана
    ComPtr<IDXGIOutput1> dxgiOutput1;
    hr = dxgiOutput.As(&dxgiOutput1);
    if (FAILED(hr)) return false;

    hr = dxgiOutput1->DuplicateOutput(d3dDevice.Get(), &deskDupl);
    if (FAILED(hr)) {
        std::cerr << "[-] Ошибка: Не удалось создать DuplicateOutput (Код: " << hr << ")." << std::endl;
        return false;
    }

    std::cout << "[+] DXGI Desktop Duplication успешно инициализирован!" << std::endl;
    return true;
}

ID3D11Texture2D* DXGICapture::AcquireFrame() {
    if (!deskDupl) return nullptr;

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = deskDupl->AcquireNextFrame(250, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return nullptr; 
    }
    else if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            std::cerr << "[-] Потерян доступ к монитору. Попытка восстановления..." << std::endl;
            Sleep(1000);
            HandleAccessLost();
        }
        return nullptr;
    }

    // Достаем интерфейс текстуры из ресурса DXGI
    ID3D11Texture2D* pTexture = nullptr;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pTexture);
    
    // Обязательно освобождаем кадр (DXGI сам следит за памятью)
    // deskDupl->ReleaseFrame();

    if (FAILED(hr)) return nullptr;
    
    // Возвращаем текстуру (нужно будет вызвать Release() после кодирования)
    return pTexture;
}

void DXGICapture::UnlockFrame() {
    if (deskDupl) {
        deskDupl->ReleaseFrame();
    }
}

bool DXGICapture::HandleAccessLost() {
    // Освобождаем сломанный интерфейс захвата
    deskDupl.Reset(); 

    // Заново получаем цепочку: Устройство -> Адаптер -> Монитор
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3dDevice.As(&dxgiDevice))) return false;

    ComPtr<IDXGIAdapter> dxgiAdapter;
    if (FAILED(dxgiDevice->GetAdapter(&dxgiAdapter))) return false;

    ComPtr<IDXGIOutput> dxgiOutput;
    // Пытаемся получить первый монитор
    if (FAILED(dxgiAdapter->EnumOutputs(0, &dxgiOutput))) return false;

    ComPtr<IDXGIOutput1> dxgiOutput1;
    if (FAILED(dxgiOutput.As(&dxgiOutput1))) return false;

    // Создаем новый дубликатор
    if (FAILED(dxgiOutput1->DuplicateOutput(d3dDevice.Get(), &deskDupl))) {
        return false;
    }

    std::cout << "[+] Доступ к монитору успешно восстановлен!" << std::endl;
    return true;
}