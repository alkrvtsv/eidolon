#include "capture/dxgi_capturer.h"
#include <string>

DXGICapturer::DXGICapturer() {
    cursorShapeBuffer_.reserve(64 * 64 * 4);
}

DXGICapturer::~DXGICapturer() noexcept {
    Shutdown();
}

bool DXGICapturer::Initialize() {
    Shutdown();

    if (!CreateD3DDevice()) {
        return false;
    }

    if (!InitializeDuplication()) {
        Shutdown();
        return false;
    }

    return true;
}

void DXGICapturer::Shutdown() noexcept {
    ReleaseFrame();
    duplication_.Reset();
    context_.Reset();
    device_.Reset();
    width_ = 0;
    height_ = 0;
}

bool DXGICapturer::CreateD3DDevice() {
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        featureLevels,
        static_cast<UINT>(std::size(featureLevels)),
        D3D11_SDK_VERSION,
        &device_,
        &featureLevel,
        &context_
    );

    return SUCCEEDED(hr);
}

bool DXGICapturer::InitializeDuplication() {
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) {
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) {
        return false;
    }

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        return false;
    }

    ComPtr<IDXGIOutputDuplication> bestDupl;
    DXGI_OUTPUT_DESC bestDesc = {};
    bool foundVirtual = false;

    ComPtr<IDXGIAdapter1> curAdapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &curAdapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; curAdapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc;
            if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop) {
                continue;
            }

            MONITORINFOEXW mi = { sizeof(mi) };
            std::wstring devString = L"";
            if (GetMonitorInfoW(desc.Monitor, &mi)) {
                DISPLAY_DEVICEW dd = { sizeof(dd) };
                if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0)) {
                    devString = dd.DeviceString;
                }
            }

            bool isVirtual = (devString.find(L"Indirect") != std::wstring::npos) ||
                             (devString.find(L"Virtual") != std::wstring::npos) ||
                             (devString.find(L"Idd") != std::wstring::npos) ||
                             (devString.find(L"VDD") != std::wstring::npos);

            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1))) {
                continue;
            }

            ComPtr<IDXGIOutputDuplication> duplTest;
            HRESULT hr = output1->DuplicateOutput(device_.Get(), &duplTest);
            if (SUCCEEDED(hr)) {
                if (isVirtual) {
                    bestDupl = duplTest;
                    bestDesc = desc;
                    foundVirtual = true;
                    break;
                }

                if (!bestDupl) {
                    bestDupl = duplTest;
                    bestDesc = desc;
                }
            }
        }
        if (foundVirtual) {
            break;
        }
    }

    if (!bestDupl) {
        return false;
    }

    duplication_ = bestDupl;
    width_ = bestDesc.DesktopCoordinates.right - bestDesc.DesktopCoordinates.left;
    height_ = bestDesc.DesktopCoordinates.bottom - bestDesc.DesktopCoordinates.top;

    return true;
}

CaptureStatus DXGICapturer::AcquireFrame(ID3D11Texture2D** ppTexture, uint32_t timeoutMs) {
    if (!ppTexture) {
        return CaptureStatus::Error;
    }
    *ppTexture = nullptr;

    if (!duplication_) {
        return CaptureStatus::AccessLost;
    }

    ReleaseFrame();

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = duplication_->AcquireNextFrame(timeoutMs, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return CaptureStatus::Timeout;
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        return CaptureStatus::AccessLost;
    }

    if (FAILED(hr)) {
        return CaptureStatus::Error;
    }

    frameLocked_ = true;

    ProcessCursor(frameInfo);

    if (frameInfo.LastPresentTime.QuadPart == 0) {
        return CaptureStatus::Timeout;
    }

    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        ReleaseFrame();
        return CaptureStatus::Error;
    }

    *ppTexture = desktopTexture.Detach();
    return CaptureStatus::Success;
}

void DXGICapturer::ReleaseFrame() {
    if (frameLocked_ && duplication_) {
        duplication_->ReleaseFrame();
        frameLocked_ = false;
    }
}

void DXGICapturer::ProcessCursor(const DXGI_OUTDUPL_FRAME_INFO& frameInfo) {
    if (frameInfo.LastMouseUpdateTime.QuadPart == 0) {
        return;
    }

    if (cursorPositionCallback_) {
        CursorPositionMessage posMsg;
        posMsg.x = frameInfo.PointerPosition.Position.x;
        posMsg.y = frameInfo.PointerPosition.Position.y;
        posMsg.visible = frameInfo.PointerPosition.Visible ? 1 : 0;
        cursorPositionCallback_(posMsg);
    }

    if (frameInfo.PointerShapeBufferSize > 0) {
        if (cursorShapeBuffer_.size() < frameInfo.PointerShapeBufferSize) {
            cursorShapeBuffer_.resize(frameInfo.PointerShapeBufferSize);
        }

        UINT requiredSize = 0;
        DXGI_OUTDUPL_POINTER_SHAPE_INFO shapeInfo;
        HRESULT hr = duplication_->GetFramePointerShape(
            frameInfo.PointerShapeBufferSize,
            cursorShapeBuffer_.data(),
            &requiredSize,
            &shapeInfo
        );

        if (SUCCEEDED(hr) && cursorShapeCallback_) {
            CursorShapeMessage shapeMsg;
            shapeMsg.width = shapeInfo.Width;
            shapeMsg.height = shapeInfo.Height;
            shapeMsg.hotspotX = shapeInfo.HotSpot.x;
            shapeMsg.hotspotY = shapeInfo.HotSpot.y;
            shapeMsg.dataSize = requiredSize;

            cursorShapeCallback_(shapeMsg, cursorShapeBuffer_.data());
        }
    }
}