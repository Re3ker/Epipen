#include "capture_session.h"
#include "overlay.h"
#include "filter_pipeline.h"
#include "diagnostics.h"
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <roapi.h>

using Microsoft::WRL::ComPtr;
namespace WGC = winrt::Windows::Graphics::Capture;
namespace WDX = winrt::Windows::Graphics::DirectX;
namespace WDX11 = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {
struct WinRtInit {
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    ~WinRtInit() { if (SUCCEEDED(hr)) RoUninitialize(); }
};
struct EventHandle {
    HANDLE value = nullptr;
    ~EventHandle() { if (value) CloseHandle(value); }
};

HMONITOR MonitorHandle(const std::wstring& deviceName) {
    struct Search { const std::wstring* name; HMONITOR result; } search{ &deviceName, nullptr };
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto* state = reinterpret_cast<Search*>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) && *state->name == info.szDevice) {
            state->result = monitor;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

HRESULT CreateDeviceForMonitor(const std::wstring& deviceName,
                               ComPtr<ID3D11Device>& device,
                               ComPtr<ID3D11DeviceContext>& context,
                               std::wstring& error) {
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { error = L"DXGI initialization failed."; return hr; }
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT j = 0;; ++j) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(j, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC desc{};
            if (SUCCEEDED(output->GetDesc(&desc)) && deviceName == desc.DeviceName) {
                D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
                D3D_FEATURE_LEVEL selected{};
                hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                    &device, &selected, &context);
                if (hr == E_INVALIDARG) {
                    hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                        D3D11_CREATE_DEVICE_BGRA_SUPPORT, &levels[1], 1, D3D11_SDK_VERSION,
                        &device, &selected, &context);
                }
                if (SUCCEEDED(hr) && selected < D3D_FEATURE_LEVEL_11_0) {
                    error = L"The selected graphics adapter does not support Direct3D feature level 11.0.";
                    return DXGI_ERROR_UNSUPPORTED;
                }
                return hr;
            }
        }
    }
    error = L"The graphics adapter for the selected monitor was not found.";
    return DXGI_ERROR_NOT_FOUND;
}

bool GetFrameTexture(const WGC::Direct3D11CaptureFrame& frame,
                     ComPtr<ID3D11Texture2D>& texture) {
    auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    return SUCCEEDED(access->GetInterface(IID_PPV_ARGS(&texture)));
}
}

CaptureResult RunCaptureSession(HWND outputWindow, const MonitorInfo& monitor,
                                HANDLE stopEvent, HANDLE settingsChangedEvent,
                                const FilterSettings& initialSettings,
                                const std::function<FilterSettings()>& getSettings,
                                const std::function<void()>& onFirstPresent) {
    CaptureResult result{};
    WinRtInit apartment;
    if (FAILED(apartment.hr)) {
        result.hr = apartment.hr;
        result.error = L"The render thread could not initialize its Windows Runtime apartment.";
        return result;
    }
    if (!WGC::GraphicsCaptureSession::IsSupported()) {
        result.hr = DXGI_ERROR_UNSUPPORTED;
        result.error = L"Windows Graphics Capture is not supported in this Windows session.";
        return result;
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    result.hr = CreateDeviceForMonitor(monitor.deviceName, device, context, result.error);
    if (FAILED(result.hr)) return result;

    OverlayPresenter presenter;
    if (!presenter.Initialize(outputWindow, monitor, device.Get(), result.error)) {
        result.hr = E_FAIL;
        return result;
    }
    FilterPipeline pipeline;
    if (!pipeline.Initialize(device.Get(), static_cast<uint32_t>(monitor.width()),
                             static_cast<uint32_t>(monitor.height()), initialSettings, result.error)) {
        result.hr = E_FAIL;
        return result;
    }
    D3D11_TEXTURE2D_DESC filteredDesc{};
    filteredDesc.Width = static_cast<UINT>(monitor.width());
    filteredDesc.Height = static_cast<UINT>(monitor.height());
    filteredDesc.MipLevels = 1;
    filteredDesc.ArraySize = 1;
    filteredDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    filteredDesc.SampleDesc.Count = 1;
    filteredDesc.Usage = D3D11_USAGE_DEFAULT;
    filteredDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> filteredOutput;
    ComPtr<ID3D11RenderTargetView> filteredOutputView;
    result.hr = device->CreateTexture2D(&filteredDesc, nullptr, &filteredOutput);
    if (FAILED(result.hr)) { result.error = L"The filtered GPU output texture could not be created."; return result; }
    result.hr = device->CreateRenderTargetView(filteredOutput.Get(), nullptr, &filteredOutputView);
    if (FAILED(result.hr)) { result.error = L"The filtered GPU output view could not be created."; return result; }

    EventHandle frameSignal{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };
    if (!frameSignal.value) {
        result.hr = HRESULT_FROM_WIN32(GetLastError());
        result.error = L"The capture event could not be created.";
        return result;
    }

    try {
        ComPtr<IDXGIDevice> dxgiDevice;
        result.hr = device.As(&dxgiDevice);
        if (FAILED(result.hr)) { result.error = L"The Direct3D device could not be connected to Windows Graphics Capture."; return result; }
        WDX11::IDirect3DDevice winrtDevice{ nullptr };
        result.hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(),
            reinterpret_cast<IInspectable**>(winrt::put_abi(winrtDevice)));
        if (FAILED(result.hr)) { result.error = L"Windows Graphics Capture could not use the Direct3D device."; return result; }

        const HMONITOR hmonitor = MonitorHandle(monitor.deviceName);
        if (!hmonitor) { result.hr = DXGI_ERROR_NOT_FOUND; result.error = L"The selected monitor is no longer connected."; return result; }
        auto interop = winrt::get_activation_factory<WGC::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        WGC::GraphicsCaptureItem item{ nullptr };
        result.hr = interop->CreateForMonitor(hmonitor, winrt::guid_of<WGC::GraphicsCaptureItem>(), winrt::put_abi(item));
        if (FAILED(result.hr)) { result.error = L"A Windows capture could not be created for the selected monitor."; return result; }

        auto pool = WGC::Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice, WDX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item.Size());
        auto frameToken = pool.FrameArrived([signal = frameSignal.value](auto const&, auto const&) { SetEvent(signal); });
        std::atomic<bool> itemClosed = false;
        auto closeToken = item.Closed([&](auto const&, auto const&) {
            itemClosed.store(true);
            SetEvent(frameSignal.value);
        });
        auto session = pool.CreateCaptureSession(item);
        session.IsCursorCaptureEnabled(false);
        session.StartCapture();

        bool forceRender = false;
        bool first = true;
        double temporalProgress = 0.0;
        auto lastRender = std::chrono::steady_clock::now();
        while (true) {
            const bool needsTemporalStep = pipeline.HasInput() && pipeline.IsTemporalEnabled() &&
                temporalProgress < 1.0 / static_cast<double>(pipeline.ChangeRate());
            const bool needsRender = pipeline.HasInput() && (forceRender || needsTemporalStep);
            HANDLE waitHandles[4] = { stopEvent, settingsChangedEvent, frameSignal.value, presenter.FrameLatencyHandle() };
            const DWORD handleCount = needsRender ? 4U : 3U;
            const DWORD wait = WaitForMultipleObjects(handleCount, waitHandles, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0) { result.stopped = true; break; }
            if (wait == WAIT_OBJECT_0 + 1) {
                const auto settings = getSettings();
                if (!pipeline.SetSettings(settings, result.error)) { result.hr = E_INVALIDARG; break; }
                if (pipeline.HasInput()) {
                    forceRender = true;
                    temporalProgress = 0.0;
                    lastRender = std::chrono::steady_clock::now();
                }
                continue;
            }
            if (wait == WAIT_OBJECT_0 + 2) {
                if (itemClosed.load()) {
                    result.hr = DXGI_ERROR_DEVICE_REMOVED;
                    result.error = L"The selected monitor was disconnected during capture.";
                    break;
                }
                WGC::Direct3D11CaptureFrame newest{ nullptr };
                uint64_t acquired = 0;
                for (;;) {
                    auto frame = pool.TryGetNextFrame();
                    if (!frame) break;
                    if (newest) ++result.supersededFrames;
                    newest = std::move(frame);
                    ++acquired;
                }
                if (!newest) continue;
                const auto size = newest.ContentSize();
                if (size.Width != monitor.width() || size.Height != monitor.height()) {
                    result.hr = DXGI_ERROR_UNSUPPORTED;
                    result.error = L"The capture size changed. The session was safely stopped; please start it again.";
                    break;
                }
                ComPtr<ID3D11Texture2D> texture;
                if (!GetFrameTexture(newest, texture)) {
                    result.hr = E_NOINTERFACE;
                    result.error = L"The capture surface could not be retrieved as a GPU texture.";
                    break;
                }
                if (!pipeline.UpdateInput(context.Get(), texture.Get(), result.error)) {
                    result.hr = DXGI_ERROR_UNSUPPORTED;
                    break;
                }
                result.capturedFrames += acquired;
                forceRender = true;
                temporalProgress = 0.0;
                lastRender = std::chrono::steady_clock::now();
                continue;
            }
            if (needsRender && wait == WAIT_OBJECT_0 + 3) {
                const auto now = std::chrono::steady_clock::now();
                const float dt = std::chrono::duration<float>(now - lastRender).count();
                lastRender = now;
                if (!pipeline.Render(context.Get(), dt, filteredOutputView.Get(), result.error)) {
                    result.hr = E_FAIL;
                    break;
                }
                if (!presenter.Present(filteredOutput.Get(), context.Get(), result.error)) { result.hr = DXGI_ERROR_DEVICE_REMOVED; break; }
                forceRender = false;
                temporalProgress += std::clamp(static_cast<double>(dt), 0.0, 1.0 / 30.0);
                ++result.presentedFrames;
                if (first) {
                    result.ready = true;
                    first = false;
                    onFirstPresent();
                }
                continue;
            }
            result.hr = HRESULT_FROM_WIN32(GetLastError());
            result.error = L"The render thread could not wait for capture or output readiness.";
            break;
        }
        session.Close();
        pool.FrameArrived(frameToken);
        item.Closed(closeToken);
        pool.Close();
    } catch (const winrt::hresult_error& exception) {
        result.hr = exception.code();
        result.error = std::wstring(L"Windows Graphics Capture stopped with an error: ") + exception.message().c_str();
    } catch (...) {
        result.hr = E_FAIL;
        result.error = L"Capture stopped with an unknown error.";
    }
    return result;
}
