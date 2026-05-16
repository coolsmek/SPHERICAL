void UIPainterImpl::text_input(const char* label, char* buffer, size_t bufferSize) {
    nk_layout_row_dynamic(m_ctx, control_row_height(), 1);
    nk_label(m_ctx, label, NK_TEXT_LEFT);
    nk_layout_row_dynamic(m_ctx, control_row_height(), 1);
    nk_edit_string_zero_terminated(m_ctx, NK_EDIT_FIELD, buffer,
                                   static_cast<int>(bufferSize), nk_filter_default);
}

