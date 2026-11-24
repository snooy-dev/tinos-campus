// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module CampusMenu;

import std;
import GameUi;

export class CampusMenu
{
public:
    std::expected<void, const char*> Initialize(IUiAssets& assets, const std::filesystem::path& content)
    {
        auto logo = assets.LoadUiImage(content / L"ui" / L"campus.png");
        if (!logo) return std::unexpected(logo.error());
        logo_ = std::move(*logo);

        constexpr std::array labels{
            std::pair{ L"Tino's Campus", 36.0f },
            std::pair{ L"일시정지", 32.0f },
            std::pair{ L"시작하기", 21.0f },
            std::pair{ L"일시정지", 18.0f },
            std::pair{ L"계속하기", 21.0f },
        };
        const std::wstring font = L"Segoe UI";
        for (std::size_t index = 0; index < labels.size(); ++index)
        {
            const auto& [text, size] = labels[index];
            auto label = assets.CreateUiText(font, size, text);
            if (!label) return std::unexpected(label.error());
            labels_[index] = std::move(*label);
        }
        interaction_.Reset(Start);
        return {};
    }

    [[nodiscard]] bool Paused() const { return screen_ != Screen::Play; }

    void Update(const UiInput& input)
    {
        const auto button = LayoutButton(input.window, Scale(input.window));
        const auto action = interaction_.Update(std::span{ &button, 1 }, input);
        if (action.button == Start || action.button == Resume ||
            (action.escape && screen_ == Screen::Pause))
        {
            screen_ = Screen::Play;
            interaction_.Reset();
        }
        else if (action.button == Pause || (action.escape && screen_ == Screen::Play))
        {
            screen_ = Screen::Pause;
            interaction_.Reset(Resume);
        }
    }

    [[nodiscard]] UiFrame Draw(UiSize window) const
    {
        UiFrame frame{ window };
        const float scale = Scale(window);
        const auto button = LayoutButton(window, scale);
        if (Paused())
        {
            frame.quads.push_back({ { 0, 0, window.width, window.height }, { .03f, .06f, .10f, .66f } });
            const bool starting = screen_ == Screen::Start;
            const auto card = PlaceUi(window, { 340 * scale, (starting ? 280 : 188) * scale }, UiAnchor::Center);
            frame.quads.push_back({ card, { .055f, .092f, .137f, .97f } });
            if (starting)
                frame.quads.push_back({ PlaceUi(window, { 76 * scale, 76 * scale }, UiAnchor::Center,
                    { 0, -66 * scale }), kWhite, logo_ });
            const auto& title = labels_[starting ? 0 : 1];
            frame.quads.push_back({ PlaceUi(window, ScaledSize(title, scale), UiAnchor::Center,
                { 0, (starting ? 11 : -38) * scale }), kWhite, title });
        }

        interaction_.DrawButton(frame, button, scale);
        return frame;
    }

private:
    enum class Screen { Start, Play, Pause };
    enum Button : UiButtonId { Start = 1, Pause, Resume };
    static constexpr UiColor kWhite{ 1, 1, 1, 1 };

    [[nodiscard]] static float Scale(UiSize window)
    {
        return std::min({ 1.0f, window.width / 360, window.height / 300 });
    }

    [[nodiscard]] static UiSize ScaledSize(const UiTextureHandle& texture, float scale)
    {
        const auto size = texture->Size();
        return { size.width * scale, size.height * scale };
    }

    [[nodiscard]] UiButton LayoutButton(UiSize window, float scale) const
    {
        switch (screen_)
        {
        case Screen::Start:
            return { Start, PlaceUi(window, { 184 * scale, 48 * scale }, UiAnchor::Center, { 0, 76 * scale }),
                labels_[2] };
        case Screen::Play:
            return { Pause, PlaceUi(window, { 112 * scale, 40 * scale }, UiAnchor::TopRight,
                { -20 * scale, 20 * scale }), labels_[3] };
        case Screen::Pause:
            return { Resume, PlaceUi(window, { 184 * scale, 48 * scale }, UiAnchor::Center, { 0, 30 * scale }),
                labels_[4] };
        }
        std::unreachable();
    }

    Screen screen_ = Screen::Start;
    UiTextureHandle logo_;
    std::array<UiTextureHandle, 5> labels_;
    UiInteraction interaction_;
};
