#pragma once

#include "SPHERICAL.h"

namespace AppTheme {

enum class ThemePresetId {
    ClassicLight = 0,
    MidnightDark = 1,
    DeepOceanBlue = 2,
    NeoGreen = 3
};

struct UIStyle {
    Spherical::UIColor panelBodyColor{1.0f, 1.0f, 1.0f, 1.0f};
    Spherical::UIColor panelTitleBarColor{0.45f, 0.45f, 0.45f, 1.0f};
    Spherical::UIColor panelBorderColor{0.0f, 0.0f, 0.0f, 1.0f};
    Spherical::UIColor panelTitleTextColor{1.0f, 1.0f, 1.0f, 1.0f};

    Spherical::UIColor textColor{0.05f, 0.05f, 0.05f, 1.0f};
    Spherical::UIColor radioButtonTextColor{0.05f, 0.05f, 0.05f, 1.0f};

    Spherical::UIColor buttonBackgroundColor{0.92f, 0.92f, 0.92f, 1.0f};
    Spherical::UIColor buttonHoverBackgroundColor{0.82f, 0.82f, 0.82f, 1.0f};
    Spherical::UIColor buttonClickedBackgroundColor{0.72f, 0.72f, 0.72f, 1.0f};
    float buttonHeight = 30.0f;
    float buttonWidth = 200.0f;
    float buttonCornerRadius = 0.0f;
    float buttonBorderThickness = 1.0f;
    Spherical::UIVec2 buttonPadding{12.0f, 7.0f};
    Spherical::ButtonStyle buttonStyle = Spherical::ButtonStyle::Embossed;
    Spherical::UIColor buttonHighlightColor{1.0f, 1.0f, 1.0f, 1.0f};
    Spherical::UIColor buttonShadowColor{0.35f, 0.35f, 0.35f, 1.0f};
    Spherical::UIColor buttonBorderColor{0.0f, 0.0f, 0.0f, 1.0f};
    Spherical::UIColor buttonTextColor{0.0f, 0.0f, 0.0f, 1.0f};

    Spherical::UIColor textInputBackgroundColor{1.0f, 1.0f, 1.0f, 1.0f};
    Spherical::UIColor textInputTextColor{0.0f, 0.0f, 0.0f, 1.0f};
};

struct ThemePreset {
    const char* name;
    UIStyle style;
};

const ThemePreset& GetThemePreset(ThemePresetId id);

} // namespace AppTheme

