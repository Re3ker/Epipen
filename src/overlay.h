#pragma once
#include "common.h"
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wrl/client.h>

class OverlayPresenter {
public:
    bool Initialize(HWND hwnd, const MonitorInfo& monitor, ID3D11Device* device, std::wstring& error);
    bool Present(ID3D11Texture2D* source, ID3D11DeviceContext* context, std::wstring& error);
    HANDLE FrameLatencyHandle() const { return m_latencyHandle; }
    void Reset();
    ~OverlayPresenter() { Reset(); }
private:
    HWND m_hwnd = nullptr;
    HANDLE m_latencyHandle = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<IDCompositionDevice> m_composition;
    Microsoft::WRL::ComPtr<IDCompositionTarget> m_target;
    Microsoft::WRL::ComPtr<IDCompositionVisual> m_visual;
};
