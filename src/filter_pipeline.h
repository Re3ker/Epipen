#pragma once
#include "common.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>

class FilterPipeline {
public:
    bool Initialize(ID3D11Device* device, uint32_t width, uint32_t height,
                    const FilterSettings& settings, std::wstring& error);
    bool UpdateInput(ID3D11DeviceContext* context, ID3D11Texture2D* source, std::wstring& error);
    bool SetSettings(const FilterSettings& settings, std::wstring& error);
    bool Render(ID3D11DeviceContext* context, float deltaSeconds,
                ID3D11RenderTargetView* output, std::wstring& error);
    bool HasInput() const { return m_hasInput; }
    bool IsTemporalEnabled() const { return m_settings.temporalEnabled; }
    float ChangeRate() const { return m_settings.maxChannelChangePerSecond; }
    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }
    void Reset();

private:
    struct alignas(16) FilterConstants {
        UINT imageWidth = 0;
        UINT imageHeight = 0;
        float deltaSeconds = 0;
        float changeRate = 1;
        float blackFloor = 0;
        float whiteCeiling = 1;
        UINT temporalEnabled = 1;
        UINT historyInitialized = 1;
        UINT padding[4]{};
    };

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_input;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_inputSrv;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 2> m_history;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2> m_historySrv;
    std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, 2> m_historyUav;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_computeShader;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constants;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizer;
    D3D11_VIEWPORT m_viewport{};
    FilterSettings m_settings{};
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_historyIndex = 0;
    bool m_hasInput = false;
};
