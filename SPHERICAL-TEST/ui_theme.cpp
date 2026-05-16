#include "ui_theme.h"

namespace AppTheme {
namespace {
const ThemePreset kClassicLight{
    "Classic Light",
    UIStyle{}
};

const ThemePreset kMidnightDark{
    "Midnight Dark",
    UIStyle{
        {0.14f, 0.14f, 0.16f, 1.0f},
        {0.18f, 0.18f, 0.20f, 1.0f},
        {0.35f, 0.35f, 0.35f, 1.0f},
        {0.95f, 0.95f, 0.95f, 1.0f},
        {0.92f, 0.92f, 0.92f, 1.0f},
        {0.92f, 0.92f, 0.92f, 1.0f},
        {0.28f, 0.28f, 0.30f, 1.0f},
        {0.36f, 0.36f, 0.38f, 1.0f},
        {0.22f, 0.45f, 0.80f, 1.0f},
        30.0f,
        200.0f,
        0.0f,
        1.0f,
        {12.0f, 7.0f},
        Spherical::ButtonStyle::Embossed,
        {0.70f, 0.70f, 0.75f, 1.0f},
        {0.08f, 0.08f, 0.10f, 1.0f},
        {0.55f, 0.55f, 0.55f, 1.0f},
        {0.14f, 0.14f, 0.14f, 1.0f},
        {0.18f, 0.18f, 0.20f, 1.0f},
        {0.95f, 0.95f, 0.95f, 1.0f}
    }
};

const ThemePreset kDeepOceanBlue{
    "Deep Ocean Blue",
    UIStyle{
        {0.06f, 0.14f, 0.22f, 1.0f},
        {0.08f, 0.22f, 0.34f, 1.0f},
        {0.24f, 0.56f, 0.78f, 1.0f},
        {0.92f, 0.98f, 1.0f, 1.0f},
        {0.90f, 0.96f, 1.0f, 1.0f},
        {0.90f, 0.96f, 1.0f, 1.0f},
        {0.10f, 0.32f, 0.48f, 1.0f},
        {0.14f, 0.44f, 0.64f, 1.0f},
        {0.06f, 0.26f, 0.40f, 1.0f},
        30.0f,
        200.0f,
        2.0f,
        1.0f,
        {12.0f, 7.0f},
        Spherical::ButtonStyle::Embossed,
        {0.58f, 0.82f, 0.96f, 1.0f},
        {0.03f, 0.10f, 0.18f, 1.0f},
        {0.30f, 0.60f, 0.82f, 1.0f},
        {0.14f, 0.14f, 0.5f, 1.0f},
        {0.08f, 0.22f, 0.34f, 1.0f},
        {0.92f, 0.98f, 1.0f, 1.0f}
    }
};
} // namespace

const ThemePreset& GetThemePreset(ThemePresetId id) {
    switch (id) {
        case ThemePresetId::ClassicLight:
            return kClassicLight;
        case ThemePresetId::MidnightDark:
            return kMidnightDark;
        case ThemePresetId::DeepOceanBlue:
            return kDeepOceanBlue;
        default:
            return kClassicLight;
    }
}

} // namespace AppTheme

