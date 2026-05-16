Spherical::UIRect UIPainterImpl::get_current_panel_bounds() const {
    return m_currentPanelBounds;
}

Spherical::UIRect UIPainterImpl::get_current_panel_content_bounds() const {
    return m_currentPanelContentBounds;
}

void UIPainterImpl::label(const char* text) {
    nk_layout_row_dynamic(m_ctx, label_row_height(), 1);
    nk_label(m_ctx, text, NK_TEXT_LEFT);
}

void UIPainterImpl::spacing() {
    nk_layout_row_dynamic(m_ctx, spacing_row_height(), 1);
    nk_spacing(m_ctx, 1);
}

