bool UIPainterImpl::radio_button(const char* label, int* activeIndex, int value) {
    if (activeIndex == nullptr) {
        // Nothing to modify.
        return false;
    }

    nk_layout_row_dynamic(m_ctx, control_row_height(), 1);

    // Remember previous selection so we can report whether this interaction changed state.
    const int prev = *activeIndex;

    if (nk_option_label(m_ctx, label, static_cast<nk_bool>(*activeIndex == value))) {
        *activeIndex = value;
    }

    return (*activeIndex != prev);
}

