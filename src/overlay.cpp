#include "overlay.h"
#include <d3d11_4.h>
#include <dxgi1_3.h>

using Microsoft::WRL::ComPtr;

bool OverlayPresenter::Initialize(HWND hwnd, const MonitorInfo& monitor, ID3D11Device* device, std::wstring& error) {
    error.clear();
    m_hwnd = hwnd;
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) { error = L"Direct3D could not be connected to DXGI."; return false; }
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) { error = L"The graphics adapter could not be queried."; return false; }
    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { error = L"The DXGI factory could not be created."; return false; }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(monitor.width());
    desc.Height = static_cast<UINT>(monitor.height());
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    hr = factory->CreateSwapChainForComposition(device, &desc, nullptr, &m_swapChain);
    if (FAILED(hr)) { error = L"The DirectComposition swap chain could not be created."; return false; }

    ComPtr<IDXGISwapChain2> swapChain2;
    hr = m_swapChain.As(&swapChain2);
    if (FAILED(hr) || FAILED(swapChain2->SetMaximumFrameLatency(1))) {
        error = L"The output queue could not be limited to one frame.";
        return false;
    }
    m_latencyHandle = swapChain2->GetFrameLatencyWaitableObject();
    if (!m_latencyHandle) { error = L"DXGI did not provide an output-ready event."; return false; }

    hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&m_composition));
    if (FAILED(hr)) { error = L"DirectComposition could not be initialized."; return false; }
    hr = m_composition->CreateTargetForHwnd(hwnd, TRUE, &m_target);
    if (FAILED(hr)) { error = L"The output window could not be attached to DirectComposition."; return false; }
    hr = m_composition->CreateVisual(&m_visual);
    if (FAILED(hr)) { error = L"The DirectComposition visual could not be created."; return false; }
    hr = m_visual->SetContent(m_swapChain.Get());
    if (FAILED(hr) || FAILED(m_target->SetRoot(m_visual.Get())) || FAILED(m_composition->Commit())) {
        error = L"The DirectComposition visual tree could not be set up.";
        return false;
    }
    return true;
}

bool OverlayPresenter::Present(ID3D11Texture2D* source, ID3D11DeviceContext* context, std::wstring& error) {
    error.clear();
    if (!m_swapChain || !source || !context) {
        error = L"The output pipeline is not initialized.";
        return false;
    }
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) { error = L"The DXGI output buffer could not be retrieved."; return false; }
    D3D11_TEXTURE2D_DESC inputDesc{}, outputDesc{};
    source->GetDesc(&inputDesc);
    backBuffer->GetDesc(&outputDesc);
    if (inputDesc.Width != outputDesc.Width || inputDesc.Height != outputDesc.Height ||
        inputDesc.Format != outputDesc.Format || inputDesc.SampleDesc.Count != 1) {
        error = L"The filtered GPU output does not match the DXGI output surface.";
        return false;
    }
    context->CopyResource(backBuffer.Get(), source);
    hr = m_swapChain->Present(1, 0);
    if (FAILED(hr)) { error = L"The filtered image could not be presented."; return false; }
    return true;
}

void OverlayPresenter::Reset() {
    if (m_latencyHandle) CloseHandle(m_latencyHandle);
    m_latencyHandle = nullptr;
    m_visual.Reset();
    m_target.Reset();
    m_composition.Reset();
    m_swapChain.Reset();
    m_hwnd = nullptr;
}
