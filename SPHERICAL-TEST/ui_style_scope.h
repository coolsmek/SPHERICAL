#pragma once

#include "ui_theme.h"

namespace AppTheme {

class ScopedUIStyle {
public:
    ScopedUIStyle(Spherical::UIPainter& ui, const UIStyle& style);
    ~ScopedUIStyle();

    ScopedUIStyle(const ScopedUIStyle&) = delete;
    ScopedUIStyle& operator=(const ScopedUIStyle&) = delete;

private:
    Spherical::UIPainter& m_ui;
};

} // namespace AppTheme

