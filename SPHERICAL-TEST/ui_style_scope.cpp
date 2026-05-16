#include "ui_style_scope.h"

namespace AppTheme {

ScopedUIStyle::ScopedUIStyle(Spherical::UIPainter& ui, const UIStyle& style)
    : m_ui(ui) {
    m_ui.push_panel_body_color(style.panelBodyColor);
    m_ui.push_panel_title_bar_color(style.panelTitleBarColor);
    m_ui.push_panel_border_color(style.panelBorderColor);
    m_ui.push_panel_title_text_color(style.panelTitleTextColor);
    m_ui.push_text_color(style.textColor);
    m_ui.push_radio_button_text_color(style.radioButtonTextColor);
    m_ui.push_button_background_color(style.buttonBackgroundColor);
    m_ui.push_button_hover_background_color(style.buttonHoverBackgroundColor);
    m_ui.push_button_clicked_background_color(style.buttonClickedBackgroundColor);
    m_ui.push_button_height(style.buttonHeight);
    m_ui.push_button_width(style.buttonWidth);
    m_ui.push_button_corner_radius(style.buttonCornerRadius);
    m_ui.push_button_border_thickness(style.buttonBorderThickness);
    m_ui.push_button_padding(style.buttonPadding);
    m_ui.push_button_style(style.buttonStyle);
    m_ui.push_button_highlight_color(style.buttonHighlightColor);
    m_ui.push_button_shadow_color(style.buttonShadowColor);
    m_ui.push_button_border_color(style.buttonBorderColor);
    m_ui.push_button_text_color(style.buttonTextColor);
    m_ui.push_text_input_background_color(style.textInputBackgroundColor);
    m_ui.push_text_input_text_color(style.textInputTextColor);
}

ScopedUIStyle::~ScopedUIStyle() {
    m_ui.pop_text_input_text_color();
    m_ui.pop_text_input_background_color();
    m_ui.pop_button_text_color();
    m_ui.pop_button_border_color();
    m_ui.pop_button_shadow_color();
    m_ui.pop_button_highlight_color();
    m_ui.pop_button_style();
    m_ui.pop_button_padding();
    m_ui.pop_button_border_thickness();
    m_ui.pop_button_corner_radius();
    m_ui.pop_button_width();
    m_ui.pop_button_height();
    m_ui.pop_button_clicked_background_color();
    m_ui.pop_button_hover_background_color();
    m_ui.pop_button_background_color();
    m_ui.pop_radio_button_text_color();
    m_ui.pop_text_color();
    m_ui.pop_panel_title_text_color();
    m_ui.pop_panel_border_color();
    m_ui.pop_panel_title_bar_color();
    m_ui.pop_panel_body_color();
}

} // namespace AppTheme

