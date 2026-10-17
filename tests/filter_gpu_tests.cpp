#include "filter_pipeline.h"
#include "filter_math.h"
#include <d3d11.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {
int failures = 0;
void Check(bool condition, const char* name) {
    if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}

struct Pixel { uint8_t b, g, r, a; };

bool ReadOutput(ID3D11DeviceContext* context, ID3D11Texture2D* output,
                ID3D11Texture2D* staging, uint32_t width, uint32_t height,
                std::vector<Pixel>& pixels) {
    context->CopyResource(staging, output);
    context->Flush();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) return false;
    pixels.resize(static_cast<size_t>(width) * height);
    for (uint32_t y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const Pixel*>(static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch);
        for (uint32_t x = 0; x < width; ++x) pixels[y * width + x] = row[x];
    }
    context->Unmap(staging, 0);
    return true;
}

int ExpectedCode(uint8_t input, const FilterSettings& settings) {
    const float inputSrgb = static_cast<float>(input) / 255.0f;
    const float filteredLinear = StaticFilter(inputSrgb, settings.blackFloorSrgb, settings.whiteCeilingSrgb);
    return static_cast<int>(std::lround(LinearToSrgb(filteredLinear) * 255.0f));
}

}

int main() {
    constexpr uint32_t width = 7;
    constexpr uint32_t height = 5;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL selected{};
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        &device, &selected, &context);
    if (hr == E_INVALIDARG) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, &levels[1], 1, D3D11_SDK_VERSION,
            &device, &selected, &context);
    }
    if (FAILED(hr) || selected < D3D_FEATURE_LEVEL_11_0) {
        std::cerr << "WARP Direct3D 11 device unavailable.\n";
        return 2;
    }

    std::vector<Pixel> source(width * height);
    const std::array<Pixel, 8> fixtures = {{
        { 0, 0, 0, 255 }, { 255, 255, 255, 255 }, { 0, 0, 255, 255 },
        { 0, 255, 0, 255 }, { 255, 0, 0, 255 }, { 128, 128, 128, 255 },
        { 25, 90, 210, 255 }, { 210, 90, 25, 255 }
    }};
    for (size_t i = 0; i < source.size(); ++i) source[i] = fixtures[i % fixtures.size()];

    D3D11_TEXTURE2D_DESC inputDesc{};
    inputDesc.Width = width;
    inputDesc.Height = height;
    inputDesc.MipLevels = 1;
    inputDesc.ArraySize = 1;
    inputDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    inputDesc.SampleDesc.Count = 1;
    inputDesc.Usage = D3D11_USAGE_DEFAULT;
    inputDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA inputData{};
    inputData.pSysMem = source.data();
    inputData.SysMemPitch = width * sizeof(Pixel);
    ComPtr<ID3D11Texture2D> input;
    hr = device->CreateTexture2D(&inputDesc, &inputData, &input);
    Check(SUCCEEDED(hr), "create BGRA capture input");
    if (FAILED(hr)) return 2;

    D3D11_TEXTURE2D_DESC outputDesc = inputDesc;
    outputDesc.Usage = D3D11_USAGE_DEFAULT;
    outputDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> output;
    ComPtr<ID3D11RenderTargetView> outputRtv;
    hr = device->CreateTexture2D(&outputDesc, nullptr, &output);
    Check(SUCCEEDED(hr), "create output texture");
    if (FAILED(hr)) return 2;
    hr = device->CreateRenderTargetView(output.Get(), nullptr, &outputRtv);
    Check(SUCCEEDED(hr), "create output render target");
    if (FAILED(hr)) return 2;

    D3D11_TEXTURE2D_DESC stagingDesc = outputDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
    Check(SUCCEEDED(hr), "create readback texture");
    if (FAILED(hr)) return 2;

    FilterSettings settings{};
    settings.blackFloorSrgb = 32;
    settings.whiteCeilingSrgb = 192;
    settings.temporalEnabled = false;
    FilterPipeline pipeline;
    std::wstring error;
    Check(pipeline.Initialize(device.Get(), width, height, settings, error), "initialize GPU filter");
    Check(pipeline.UpdateInput(context.Get(), input.Get(), error), "upload capture frame");
    Check(pipeline.Render(context.Get(), 1.0f / 60.0f, outputRtv.Get(), error), "run static GPU filter");
    std::vector<Pixel> actual;
    Check(ReadOutput(context.Get(), output.Get(), staging.Get(), width, height, actual), "read filtered output");
    if (actual.size() == source.size()) {
        for (size_t i = 0; i < actual.size(); ++i) {
            Check(std::abs(static_cast<int>(actual[i].r) - ExpectedCode(source[i].r, settings)) <= 2,
                           "red channel follows sRGB filter curve");
            Check(std::abs(static_cast<int>(actual[i].g) - ExpectedCode(source[i].g, settings)) <= 2,
                           "green channel follows sRGB filter curve");
            Check(std::abs(static_cast<int>(actual[i].b) - ExpectedCode(source[i].b, settings)) <= 2,
                           "blue channel follows sRGB filter curve");
            Check(actual[i].a == 255, "filtered output alpha is opaque");
        }
    }

    settings.blackFloorSrgb = 64;
    settings.whiteCeilingSrgb = 128;
    Check(pipeline.SetSettings(settings, error), "apply live GPU filter settings");
    Check(pipeline.Render(context.Get(), 1.0f / 60.0f, outputRtv.Get(), error), "render with changed settings");
    Check(ReadOutput(context.Get(), output.Get(), staging.Get(), width, height, actual), "read output after settings change");
    if (actual.size() == source.size()) {
        for (size_t i = 0; i < actual.size(); ++i) {
            Check(std::abs(static_cast<int>(actual[i].r) - ExpectedCode(source[i].r, settings)) <= 2,
                           "live settings update changes red output");
            Check(std::abs(static_cast<int>(actual[i].g) - ExpectedCode(source[i].g, settings)) <= 2,
                           "live settings update changes green output");
            Check(std::abs(static_cast<int>(actual[i].b) - ExpectedCode(source[i].b, settings)) <= 2,
                           "live settings update changes blue output");
        }
    }

    FilterSettings dynamicSettings{};
    dynamicSettings.blackFloorSrgb = 0;
    dynamicSettings.whiteCeilingSrgb = 255;
    dynamicSettings.temporalEnabled = true;
    dynamicSettings.maxChannelChangePerSecond = 1.0f;
    FilterPipeline dynamicPipeline;
    Check(dynamicPipeline.Initialize(device.Get(), width, height, dynamicSettings, error), "initialize temporal GPU filter");
    std::vector<Pixel> white(width * height, Pixel{ 255, 255, 255, 255 });
    inputData.pSysMem = white.data();
    hr = device->CreateTexture2D(&inputDesc, &inputData, &input);
    Check(SUCCEEDED(hr), "create white capture frame");
    Check(dynamicPipeline.UpdateInput(context.Get(), input.Get(), error), "upload white frame");
    Check(dynamicPipeline.Render(context.Get(), 1.0f / 60.0f, outputRtv.Get(), error), "run temporal GPU filter");
    Check(ReadOutput(context.Get(), output.Get(), staging.Get(), width, height, actual), "read temporal output");
    if (!actual.empty()) {
        const float quietLinear = SrgbToLinear(32.0f / 255.0f);
        const float budget = dynamicSettings.maxChannelChangePerSecond / 60.0f;
        for (const auto& pixel : actual) {
            const float red = SrgbToLinear(pixel.r / 255.0f);
            const float green = SrgbToLinear(pixel.g / 255.0f);
            const float blue = SrgbToLinear(pixel.b / 255.0f);
            Check(red > quietLinear && red - quietLinear <= budget + 0.001f,
                  "temporal red change is rate limited");
            Check(green > quietLinear && green - quietLinear <= budget + 0.001f,
                  "temporal green change is rate limited");
            Check(blue > quietLinear && blue - quietLinear <= budget + 0.001f,
                  "temporal blue change is rate limited");
        }
    }

    if (failures) return 1;
    std::cout << "GPU filter output checks passed.\n";
    return 0;
}
