// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module;
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

module GameUi;

namespace
{
using Microsoft::WRL::ComPtr;
constexpr UINT kMaxTextureSize = 4096;
constexpr float kTextScale = 2.0f;

struct Imaging
{
    HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> factory;

    Imaging()
    {
        if (SUCCEEDED(apartment) || apartment == RPC_E_CHANGED_MODE)
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.GetAddressOf()));
    }

    ~Imaging()
    {
        factory.Reset();
        if (SUCCEEDED(apartment)) CoUninitialize();
    }
};

std::expected<UiTextureHandle, const char*> ReadPixels(IWICBitmapSource& source, float scale)
{
    UINT width = 0, height = 0;
    if (FAILED(source.GetSize(&width, &height)) ||
        !width || !height || width > kMaxTextureSize || height > kMaxTextureSize)
        return std::unexpected("UI bitmap dimensions must be within 1–4096 pixels.");

    std::vector<std::byte> pixels(width * height * 4);
    if (FAILED(source.CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()),
        reinterpret_cast<BYTE*>(pixels.data()))))
        return std::unexpected("Cannot read UI bitmap pixels.");

    return std::make_shared<const UiTexture>(UiSize{ width / scale, height / scale },
        width, height, std::move(pixels));
}
}

std::expected<UiTextureHandle, const char*> UiAssets::LoadUiImage(const std::filesystem::path& path)
{
    Imaging imaging;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    GUID format{};
    if (!imaging.factory ||
        FAILED(imaging.factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) ||
        FAILED(decoder->GetContainerFormat(&format)) || format != GUID_ContainerFormatPng ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())) ||
        FAILED(imaging.factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return std::unexpected("Cannot decode UI PNG image.");

    return ReadPixels(*converter.Get(), 1.0f);
}

std::expected<UiTextureHandle, const char*> UiAssets::CreateUiText(const std::wstring& family,
    float size, std::wstring_view text)
{
    if (family.empty() || family.contains(L'\0') || !std::isfinite(size) || size <= 0 || size > 256 ||
        text.empty() || text.size() > 2048 || text.find_first_of(L"\r\n") != std::wstring_view::npos ||
        text.contains(L'\0'))
        return std::unexpected("Invalid single-line UI text, font family or size.");

    Imaging imaging;
    ComPtr<IDWriteFactory> write;
    ComPtr<IDWriteFontCollection> fonts;
    ComPtr<IDWriteTextFormat> format;
    ComPtr<IDWriteTextLayout> layout;
    UINT32 font_index = 0;
    BOOL font_exists = FALSE;
    constexpr float max_extent = kMaxTextureSize / kTextScale;
    if (!imaging.factory ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write.GetAddressOf()))) ||
        FAILED(write->GetSystemFontCollection(fonts.GetAddressOf())) ||
        FAILED(fonts->FindFamilyName(family.c_str(), &font_index, &font_exists)) || !font_exists ||
        FAILED(write->CreateTextFormat(family.c_str(), fonts.Get(), DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"ko-KR", format.GetAddressOf())) ||
        FAILED(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP)) ||
        FAILED(write->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format.Get(),
            max_extent, max_extent, layout.GetAddressOf())))
        return std::unexpected("Cannot create UI text layout with the requested system font.");

    DWRITE_TEXT_METRICS metrics{};
    DWRITE_OVERHANG_METRICS overhang{};
    if (FAILED(layout->GetMetrics(&metrics)) || FAILED(layout->GetOverhangMetrics(&overhang)))
        return std::unexpected("Cannot measure UI text layout.");

    const float x = std::max(overhang.left, 0.0f) + 1;
    const float y = std::max(overhang.top, 0.0f) + 1;
    const float width = std::ceil(kTextScale *
        (x + std::max(metrics.left + metrics.widthIncludingTrailingWhitespace, max_extent + overhang.right) + 1));
    const float height = std::ceil(kTextScale *
        (y + std::max(metrics.top + metrics.height, max_extent + overhang.bottom) + 1));
    if (!(width > 0 && height > 0 && width <= kMaxTextureSize && height <= kMaxTextureSize))
        return std::unexpected("UI text exceeds the maximum bitmap size.");

    ComPtr<ID2D1Factory> draw;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96 * kTextScale, 96 * kTextScale);
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, draw.GetAddressOf())) ||
        FAILED(imaging.factory->CreateBitmap(static_cast<UINT>(width), static_cast<UINT>(height),
            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.GetAddressOf())) ||
        FAILED(draw->CreateWicBitmapRenderTarget(bitmap.Get(), properties, target.GetAddressOf())) ||
        FAILED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush.GetAddressOf())))
        return std::unexpected("Cannot create UI text bitmap.");

    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    target->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush.Get());
    if (FAILED(target->EndDraw()))
        return std::unexpected("Cannot rasterize UI text.");

    return ReadPixels(*bitmap.Get(), kTextScale);
}
