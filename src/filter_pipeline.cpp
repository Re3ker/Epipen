#include "filter_pipeline.h"
#include "filter_math.h"
#include "filter_compute.h"
#include "filter_vs.h"
#include "filter_ps.h"
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

bool FilterPipeline::Initialize(ID3D11Device* device, uint32_t width, uint32_t height,
                                const FilterSettings& settings, std::wstring& error) {
    error.clear();
    Reset();
    if (!device || !width || !height || !ValidateSettings(settings)) {
        error = L"Ungültige Größe, Direct3D-Gerät oder Filtereinstellungen für die Filterpipeline.";
        return false;
    }
    m_device = device;
    m_width = width;
    m_height = height;
    m_settings = settings;

    HRESULT hr = m_device->CreateComputeShader(g_filterCompute, sizeof(g_filterCompute), nullptr, &m_computeShader);
    if (FAILED(hr)) { error = L"Der GPU-Filtershader konnte nicht erstellt werden."; Reset(); return false; }
    hr = m_device->CreateVertexShader(g_filterVS, sizeof(g_filterVS), nullptr, &m_vertexShader);
    if (FAILED(hr)) { error = L"Der Vollbild-Vertexshader konnte nicht erstellt werden."; Reset(); return false; }
    hr = m_device->CreatePixelShader(g_filterPS, sizeof(g_filterPS), nullptr, &m_pixelShader);
    if (FAILED(hr)) { error = L"Der Farbausgabe-Pixelshader konnte nicht erstellt werden."; Reset(); return false; }

    D3D11_TEXTURE2D_DESC inputDesc{};
    inputDesc.Width = width;
    inputDesc.Height = height;
    inputDesc.MipLevels = 1;
    inputDesc.ArraySize = 1;
    inputDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    inputDesc.SampleDesc.Count = 1;
    inputDesc.Usage = D3D11_USAGE_DEFAULT;
    inputDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = m_device->CreateTexture2D(&inputDesc, nullptr, &m_input);
    if (FAILED(hr)) { error = L"Der GPU-Eingabepuffer konnte nicht erstellt werden."; Reset(); return false; }
    hr = m_device->CreateShaderResourceView(m_input.Get(), nullptr, &m_inputSrv);
    if (FAILED(hr)) { error = L"Die Shaderansicht des GPU-Eingabepuffers konnte nicht erstellt werden."; Reset(); return false; }

    D3D11_TEXTURE2D_DESC historyDesc{};
    historyDesc.Width = width;
    historyDesc.Height = height;
    historyDesc.MipLevels = 1;
    historyDesc.ArraySize = 1;
    historyDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    historyDesc.SampleDesc.Count = 1;
    historyDesc.Usage = D3D11_USAGE_DEFAULT;
    historyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    const float quiet = SrgbToLinear(32.0f / 255.0f);
    const float initial[4] = { quiet, quiet, quiet, 1.0f };
    for (size_t i = 0; i < m_history.size(); ++i) {
        hr = m_device->CreateTexture2D(&historyDesc, nullptr, &m_history[i]);
        if (FAILED(hr)) { error = L"Der GPU-Verlaufspuffer konnte nicht erstellt werden."; Reset(); return false; }
        hr = m_device->CreateShaderResourceView(m_history[i].Get(), nullptr, &m_historySrv[i]);
        if (FAILED(hr)) { error = L"Die Shaderansicht des GPU-Verlaufspuffers konnte nicht erstellt werden."; Reset(); return false; }
        hr = m_device->CreateUnorderedAccessView(m_history[i].Get(), nullptr, &m_historyUav[i]);
        if (FAILED(hr)) { error = L"Die Schreibansicht des GPU-Verlaufspuffers konnte nicht erstellt werden."; Reset(); return false; }
    }
    ComPtr<ID3D11DeviceContext> immediate;
    m_device->GetImmediateContext(&immediate);
    for (const auto& uav : m_historyUav) immediate->ClearUnorderedAccessViewFloat(uav.Get(), initial);

    D3D11_BUFFER_DESC constantsDesc{};
    constantsDesc.ByteWidth = sizeof(FilterConstants);
    constantsDesc.Usage = D3D11_USAGE_DEFAULT;
    constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = m_device->CreateBuffer(&constantsDesc, nullptr, &m_constants);
    if (FAILED(hr)) { error = L"Der GPU-Konfigurationspuffer konnte nicht erstellt werden."; Reset(); return false; }

    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;
    hr = m_device->CreateRasterizerState(&rasterDesc, &m_rasterizer);
    if (FAILED(hr)) { error = L"Der GPU-Ausgabezustand konnte nicht erstellt werden."; Reset(); return false; }

    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;
    m_viewport.Width = static_cast<float>(width);
    m_viewport.Height = static_cast<float>(height);
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;
    m_historyIndex = 0;
    m_hasInput = false;
    return true;
}

bool FilterPipeline::UpdateInput(ID3D11DeviceContext* context, ID3D11Texture2D* source, std::wstring& error) {
    error.clear();
    if (!context || !source || !m_input) { error = L"Der GPU-Eingabepuffer ist nicht bereit."; return false; }
    D3D11_TEXTURE2D_DESC sourceDesc{};
    source->GetDesc(&sourceDesc);
    if (sourceDesc.Width != m_width || sourceDesc.Height != m_height ||
        sourceDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || sourceDesc.SampleDesc.Count != 1) {
        error = L"Die Aufnahmegröße oder das Pixelformat passt nicht zur GPU-Filterpipeline.";
        return false;
    }
    context->CopyResource(m_input.Get(), source);
    m_hasInput = true;
    return true;
}

bool FilterPipeline::SetSettings(const FilterSettings& settings, std::wstring& error) {
    error.clear();
    if (!ValidateSettings(settings)) { error = L"Ungültige Filtereinstellungen wurden zurückgewiesen."; return false; }
    m_settings = settings;
    return true;
}

bool FilterPipeline::Render(ID3D11DeviceContext* context, float deltaSeconds,
                            ID3D11RenderTargetView* output, std::wstring& error) {
    error.clear();
    if (!context || !output || !m_computeShader || !m_hasInput) {
        error = L"Die GPU-Filterpipeline ist nicht vollständig initialisiert.";
        return false;
    }

    const uint32_t nextIndex = 1U - m_historyIndex;
    FilterConstants constants{};
    constants.imageWidth = m_width;
    constants.imageHeight = m_height;
    constants.deltaSeconds = std::clamp(deltaSeconds, 0.0f, 1.0f / 30.0f);
    constants.changeRate = m_settings.maxChannelChangePerSecond;
    constants.blackFloor = static_cast<float>(m_settings.blackFloorSrgb) / 255.0f;
    constants.whiteCeiling = static_cast<float>(m_settings.whiteCeilingSrgb) / 255.0f;
    constants.temporalEnabled = m_settings.temporalEnabled ? 1U : 0U;
    context->UpdateSubresource(m_constants.Get(), 0, nullptr, &constants, 0, 0);

    ID3D11ShaderResourceView* computeSrvs[] = { m_historySrv[m_historyIndex].Get(), m_inputSrv.Get() };
    ID3D11UnorderedAccessView* computeUav = m_historyUav[nextIndex].Get();
    ID3D11Buffer* constantBuffer = m_constants.Get();
    context->CSSetShader(m_computeShader.Get(), nullptr, 0);
    context->CSSetShaderResources(0, ARRAYSIZE(computeSrvs), computeSrvs);
    context->CSSetUnorderedAccessViews(0, 1, &computeUav, nullptr);
    context->CSSetConstantBuffers(0, 1, &constantBuffer);
    context->Dispatch((m_width + 7U) / 8U, (m_height + 7U) / 8U, 1);

    ID3D11ShaderResourceView* nullSrvs[2]{};
    ID3D11UnorderedAccessView* nullUav = nullptr;
    context->CSSetShaderResources(0, ARRAYSIZE(nullSrvs), nullSrvs);
    context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
    context->CSSetShader(nullptr, nullptr, 0);

    ID3D11ShaderResourceView* historySrv = m_historySrv[nextIndex].Get();
    context->OMSetRenderTargets(1, &output, nullptr);
    context->RSSetViewports(1, &m_viewport);
    context->RSSetState(m_rasterizer.Get());
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    context->PSSetShaderResources(3, 1, &historySrv);
    context->Draw(3, 0);
    ID3D11ShaderResourceView* nullHistory = nullptr;
    context->PSSetShaderResources(3, 1, &nullHistory);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    m_historyIndex = nextIndex;
    return true;
}

void FilterPipeline::Reset() {
    m_rasterizer.Reset();
    m_constants.Reset();
    m_pixelShader.Reset();
    m_vertexShader.Reset();
    m_computeShader.Reset();
    for (auto& uav : m_historyUav) uav.Reset();
    for (auto& srv : m_historySrv) srv.Reset();
    for (auto& texture : m_history) texture.Reset();
    m_inputSrv.Reset();
    m_input.Reset();
    m_device.Reset();
    m_width = m_height = m_historyIndex = 0;
    m_hasInput = false;
}
