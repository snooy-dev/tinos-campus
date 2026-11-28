// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module GameUi;

import std;

export struct UiSize { float width = 0, height = 0; };
export struct UiPoint { float x = 0, y = 0; };
export struct UiRect
{
    float x, y, width, height;

    [[nodiscard]] bool Contains(UiPoint point) const
    {
        return point.x >= x && point.y >= y && point.x < x + width && point.y < y + height;
    }
};

export enum class UiAnchor { TopLeft, TopRight, BottomLeft, BottomRight, Center };

export [[nodiscard]] inline UiRect PlaceUi(UiSize window, UiSize size, UiAnchor anchor,
    UiPoint offset = {})
{
    const float right = window.width - size.width;
    const float bottom = window.height - size.height;
    switch (anchor)
    {
    case UiAnchor::TopLeft: break;
    case UiAnchor::TopRight: offset.x += right; break;
    case UiAnchor::BottomLeft: offset.y += bottom; break;
    case UiAnchor::BottomRight: offset.x += right; offset.y += bottom; break;
    case UiAnchor::Center: offset.x += right / 2; offset.y += bottom / 2; break;
    }
    return { offset.x, offset.y, size.width, size.height };
}

export class UiTexture
{
public:
    UiTexture(UiSize size, unsigned width, unsigned height, std::vector<std::byte> pixels)
        : size_(size), width_(width), height_(height), pixels_(std::move(pixels)) {}

    [[nodiscard]] UiSize Size() const { return size_; }
    [[nodiscard]] unsigned PixelWidth() const { return width_; }
    [[nodiscard]] unsigned PixelHeight() const { return height_; }
    [[nodiscard]] std::span<const std::byte> Pixels() const { return pixels_; }

private:
    UiSize size_;
    unsigned width_, height_;
    std::vector<std::byte> pixels_;
};

export using UiTextureHandle = std::shared_ptr<const UiTexture>;
export using UiColor = std::array<float, 4>;

export struct UiQuad
{
    UiRect rect;
    UiColor color{ 1, 1, 1, 1 };
    UiTextureHandle texture;
};

export struct UiFrame
{
    UiSize window;
    // Later quads are painted over earlier quads.
    std::vector<UiQuad> quads;
};

export class IUiAssets
{
public:
    virtual ~IUiAssets() = default;
    [[nodiscard]] virtual std::expected<UiTextureHandle, const char*> LoadUiImage(
        const std::filesystem::path& path) = 0;
    [[nodiscard]] virtual std::expected<UiTextureHandle, const char*> CreateUiText(
        const std::wstring& family, float size, std::wstring_view text) = 0;
};

export class UiAssets final : public IUiAssets
{
public:
    std::expected<UiTextureHandle, const char*> LoadUiImage(const std::filesystem::path& path) override;
    std::expected<UiTextureHandle, const char*> CreateUiText(const std::wstring& family,
        float size, std::wstring_view text) override;
};

export enum class UiEventType { Press, Release, Next, Previous, Activate, Escape, Cancel };
export struct UiEvent { UiEventType type; UiPoint cursor{}; };

export struct UiInput
{
    UiSize window;
    UiPoint cursor;
    bool pointer_available = false;
    bool focused = false;
    std::vector<UiEvent> events;
};

export using UiButtonId = unsigned;
export struct UiButton
{
    UiButtonId id; // Nonzero and unique on the current screen.
    UiRect rect;
    UiTextureHandle label;
};

export struct UiAction { UiButtonId button = 0; bool escape = false; };

export class UiInteraction
{
public:
    [[nodiscard]] UiAction Update(std::span<const UiButton> buttons, const UiInput& input)
    {
        if (!input.focused)
        {
            Reset();
            return {};
        }

        const auto find_id = [&](UiButtonId id) {
            return std::ranges::find(buttons, id, &UiButton::id);
        };
        if (find_id(focused_) == buttons.end()) focused_ = 0;
        if (find_id(pressed_) == buttons.end() || !input.pointer_available) pressed_ = 0;

        const auto hit = [&](UiPoint point) -> UiButtonId {
            if (input.pointer_available)
            {
                for (const auto& button : buttons | std::views::reverse)
                    if (button.rect.Contains(point)) return button.id;
            }
            return 0;
        };
        hovered_ = hit(input.cursor);

        for (const auto& event : input.events)
        {
            switch (event.type)
            {
            case UiEventType::Press:
                pressed_ = hit(event.cursor);
                if (pressed_) focused_ = pressed_;
                break;
            case UiEventType::Release:
            {
                const auto pressed = std::exchange(pressed_, 0);
                if (pressed && pressed == hit(event.cursor)) return { pressed };
                break;
            }
            case UiEventType::Next:
            case UiEventType::Previous:
            {
                if (buttons.empty()) break;
                const bool reverse = event.type == UiEventType::Previous;
                const auto current = find_id(focused_);
                const auto index = current == buttons.end()
                    ? (reverse ? buttons.size() - 1 : 0)
                    : (static_cast<std::size_t>(current - buttons.begin()) +
                        (reverse ? buttons.size() - 1 : 1)) % buttons.size();
                focused_ = buttons[index].id;
                break;
            }
            case UiEventType::Activate:
                if (focused_)
                {
                    pressed_ = 0;
                    return { focused_ };
                }
                break;
            case UiEventType::Escape:
                pressed_ = 0;
                return { 0, true };
            case UiEventType::Cancel:
                Reset();
                break;
            }
        }
        return {};
    }

    void Reset(UiButtonId focus = 0)
    {
        hovered_ = pressed_ = 0;
        focused_ = focus;
    }

    void DrawButton(UiFrame& frame, const UiButton& button, float scale = 1.0f) const
    {
        const auto& bounds = button.rect;
        if (Focused(button.id))
        {
            const float border = 2 * scale;
            frame.quads.push_back({ { bounds.x - border, bounds.y - border,
                bounds.width + 2 * border, bounds.height + 2 * border }, { 1, .85f, .44f, 1 } });
        }
        const UiColor background = Pressed(button.id) ? UiColor{ .08f, .30f, .33f, 1 }
            : Hovered(button.id) ? UiColor{ .17f, .50f, .53f, 1 } : UiColor{ .12f, .37f, .41f, 1 };
        frame.quads.push_back({ bounds, background });
        if (button.label)
        {
            const auto size = button.label->Size();
            const float width = size.width * scale;
            const float height = size.height * scale;
            frame.quads.push_back({ { bounds.x + (bounds.width - width) / 2,
                bounds.y + (bounds.height - height) / 2, width, height }, { 1, 1, 1, 1 }, button.label });
        }
    }

    [[nodiscard]] bool Hovered(UiButtonId id) const { return hovered_ == id; }
    [[nodiscard]] bool Pressed(UiButtonId id) const { return pressed_ == id && hovered_ == id; }
    [[nodiscard]] bool Focused(UiButtonId id) const { return focused_ == id; }

private:
    UiButtonId hovered_ = 0, pressed_ = 0, focused_ = 0;
};
