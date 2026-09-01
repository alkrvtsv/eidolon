#include "capture/dxgi_capturer.h"
#include <iostream>

DXGICapturer::DXGICapturer() {
    shapeBuffer_.resize(64 * 64 * 4);
    convertedShapeBuffer_.resize(64 * 64 * 4);
}

DXGICapturer::~DXGICapturer() noexcept {
    Shutdown();
}

bool DXGICapturer::Initialize() {
    Shutdown();

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION, &device_, &featureLevel, &context_
    );

    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    device_.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> dxgiAdapter;
    dxgiDevice->GetAdapter(&dxgiAdapter);

    ComPtr<IDXGIOutput> dxgiOutput;
    if (FAILED(dxgiAdapter->EnumOutputs(0, &dxgiOutput))) return false;

    ComPtr<IDXGIOutput1> dxgiOutput1;
    dxgiOutput.As(&dxgiOutput1);

    DXGI_OUTPUT_DESC outDesc;
    dxgiOutput->GetDesc(&outDesc);
    width_ = outDesc.DesktopCoordinates.right - outDesc.DesktopCoordinates.left;
    height_ = outDesc.DesktopCoordinates.bottom - outDesc.DesktopCoordinates.top;

    hr = dxgiOutput1->DuplicateOutput(device_.Get(), &duplication_);
    return SUCCEEDED(hr);
}

void DXGICapturer::Shutdown() noexcept {
    ReleaseFrame();
    duplication_.Reset();
    context_.Reset();
    device_.Reset();
}

void DXGICapturer::ResendCursorState() {
    if (hasCachedShape_ && cursorShapeCallback_ && !cachedShapeData_.empty()) {
        cursorShapeCallback_(cachedShape_, cachedShapeData_.data());
    }
    if (hasCachedPos_ && cursorPositionCallback_) {
        cursorPositionCallback_(cachedPos_);
    }
}

CaptureStatus DXGICapturer::AcquireFrame(ID3D11Texture2D** outTexture, uint32_t timeoutMs) {
    if (!duplication_ || !outTexture) return CaptureStatus::Error;

    ReleaseFrame();

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = duplication_->AcquireNextFrame(timeoutMs, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        ProcessCursor(frameInfo);
        return CaptureStatus::Timeout;
    }

    if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
        return CaptureStatus::AccessLost;
    }

    if (FAILED(hr)) {
        return CaptureStatus::Error;
    }

    frameAcquired_ = true;
    hr = desktopResource.As(&currentFrameTexture_);
    if (FAILED(hr)) {
        ReleaseFrame();
        return CaptureStatus::Error;
    }

    ProcessCursor(frameInfo);

    *outTexture = currentFrameTexture_.Get();
    (*outTexture)->AddRef();
    return CaptureStatus::Success;
}

void DXGICapturer::ReleaseFrame() {
    if (frameAcquired_ && duplication_) {
        currentFrameTexture_.Reset();
        duplication_->ReleaseFrame();
        frameAcquired_ = false;
    }
}

void DXGICapturer::ProcessCursor(const DXGI_OUTDUPL_FRAME_INFO& frameInfo) {
    CURSORINFO ci = { sizeof(CURSORINFO) };
    bool isShowing = true;
    if (GetCursorInfo(&ci)) {
        isShowing = (ci.flags & CURSOR_SHOWING) != 0;
    }

    // Проверяем изменение позиции или глобального статуса видимости игры
    if (frameInfo.LastMouseUpdateTime.QuadPart != 0 || isShowing != (cachedPos_.visible != 0)) {
        cachedPos_.type = MessageType::CursorPosition;
        if (frameInfo.PointerPosition.Visible) {
            cachedPos_.x = frameInfo.PointerPosition.Position.x;
            cachedPos_.y = frameInfo.PointerPosition.Position.y;
        } else if (ci.flags & CURSOR_SHOWING) {
            cachedPos_.x = ci.ptScreenPos.x;
            cachedPos_.y = ci.ptScreenPos.y;
        }
        cachedPos_.visible = isShowing ? 1 : 0;
        hasCachedPos_ = true;

        if (cursorPositionCallback_) {
            cursorPositionCallback_(cachedPos_);
        }
    }

    if (frameInfo.PointerShapeBufferSize > 0) {
        if (shapeBuffer_.size() < frameInfo.PointerShapeBufferSize) {
            shapeBuffer_.resize(frameInfo.PointerShapeBufferSize);
        }

        UINT requiredSize = 0;
        DXGI_OUTDUPL_POINTER_SHAPE_INFO shapeInfo;
        HRESULT hr = duplication_->GetFramePointerShape(
            frameInfo.PointerShapeBufferSize,
            shapeBuffer_.data(),
            &requiredSize,
            &shapeInfo
        );

        if (SUCCEEDED(hr)) {
            CursorShapeMessage msg;
            msg.type = MessageType::CursorShape;
            msg.width = shapeInfo.Width;
            msg.height = shapeInfo.Height;
            msg.hotspotX = shapeInfo.HotSpot.x;
            msg.hotspotY = shapeInfo.HotSpot.y;

            if (shapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
                msg.dataSize = shapeInfo.Width * shapeInfo.Height * 4;
                cachedShape_ = msg;
                cachedShapeData_.assign(shapeBuffer_.data(), shapeBuffer_.data() + msg.dataSize);
                hasCachedShape_ = true;

                if (cursorShapeCallback_) {
                    cursorShapeCallback_(msg, shapeBuffer_.data());
                }
            } else if (shapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) {
                UINT actualHeight = shapeInfo.Height / 2;
                msg.height = actualHeight;
                msg.dataSize = shapeInfo.Width * actualHeight * 4;

                if (convertedShapeBuffer_.size() < msg.dataSize) {
                    convertedShapeBuffer_.resize(msg.dataSize);
                }

                auto* dst = reinterpret_cast<uint32_t*>(convertedShapeBuffer_.data());
                const uint8_t* andMask = shapeBuffer_.data();
                const uint8_t* xorMask = shapeBuffer_.data() + (shapeInfo.Pitch * actualHeight);

                for (UINT row = 0; row < actualHeight; ++row) {
                    for (UINT col = 0; col < shapeInfo.Width; ++col) {
                        UINT byteIdx = (row * shapeInfo.Pitch) + (col / 8);
                        UINT bitMask = 0x80 >> (col % 8);

                        bool andBit = (andMask[byteIdx] & bitMask) != 0;
                        bool xorBit = (xorMask[byteIdx] & bitMask) != 0;

                        if (!andBit && !xorBit) {
                            dst[row * shapeInfo.Width + col] = 0xFF000000;
                        } else if (!andBit && xorBit) {
                            dst[row * shapeInfo.Width + col] = 0xFFFFFFFF;
                        } else {
                            dst[row * shapeInfo.Width + col] = 0x00000000;
                        }
                    }
                }

                cachedShape_ = msg;
                cachedShapeData_.assign(convertedShapeBuffer_.data(), convertedShapeBuffer_.data() + msg.dataSize);
                hasCachedShape_ = true;

                if (cursorShapeCallback_) {
                    cursorShapeCallback_(msg, convertedShapeBuffer_.data());
                }
            }
        }
    }
}