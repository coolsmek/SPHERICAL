bool UIPainterImpl::button(const char* label) {
    nk_layout_row_dynamic(m_ctx, button_row_height(), 1);
    return draw_custom_button(label != nullptr ? label : "");
}

