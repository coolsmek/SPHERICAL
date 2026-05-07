# Nuklear.h Map

## CONSTANTS (Lines 235 - 255)

## HELPER (Lines 255 - 334)

## BASIC (Lines 334 - 456)

## API (Lines 456 - 563)
**Structs**: nk_color, nk_colorf, nk_vec2, nk_vec2i, nk_rect, nk_recti, nk_image, nk_nine_slice, nk_cursor, nk_scroll, nk_allocator
**Enums**: nk_heading, nk_button_behavior, nk_modify, nk_orientation, nk_collapse_states, nk_show_states, nk_chart_type, nk_chart_event, nk_color_format, nk_popup_type, nk_layout_format, nk_tree_type, nk_tooltip_pos, nk_symbol_type

## CONTEXT (Lines 563 - 730)
**Functions**: 
- `nk_init_default`
- `nk_init_fixed`
- `nk_init`
- `nk_init_custom`
- `nk_clear`
- `nk_free`
- `nk_set_user_data`

## INPUT (Lines 730 - 980)
**Enums**: nk_keys, nk_buttons
**Functions**: 
- `nk_input_begin`
- `nk_input_motion`
- `nk_input_key`
- `nk_input_button`
- `nk_input_scroll`
- `nk_input_char`
- `nk_input_glyph`
- `nk_input_unicode`
- `nk_input_end`

## DRAWING (Lines 980 - 1382)
**Structs**: nk_draw_null_texture, nk_convert_config
**Enums**: nk_anti_aliasing, nk_convert_result
**Functions**: 
- `const  nk_command nk__begin`
- `const  nk_command nk__next`
- `nk_flags nk_convert`
- `const  nk_draw_command nk__draw_begin`
- `const  nk_draw_command nk__draw_end`
- `const  nk_draw_command nk__draw_next`

## WINDOW (Lines 1382 - 2184)
**Enums**: nk_panel_flags
**Functions**: 
- `nk_begin`
- `nk_begin_titled`
- `nk_end`
- `nk_window nk_window_find`
- `nk_rect nk_window_get_bounds`
- `nk_vec2 nk_window_get_position`
- `nk_vec2 nk_window_get_size`
- `nk_window_get_width`
- `nk_window_get_height`
- `nk_panel nk_window_get_panel`
- `nk_rect nk_window_get_content_region`
- `nk_vec2 nk_window_get_content_region_min`
- `nk_vec2 nk_window_get_content_region_max`
- `nk_vec2 nk_window_get_content_region_size`
- `nk_command_buffer nk_window_get_canvas`
- `nk_window_get_scroll`
- `nk_window_has_focus`
- `nk_window_is_hovered`
- `nk_window_is_collapsed`
- `nk_window_is_closed`
- `nk_window_is_hidden`
- `nk_window_is_active`
- `nk_window_is_any_hovered`
- `nk_item_is_any_active`
- `nk_window_set_bounds`
- `nk_window_set_position`
- `nk_window_set_size`
- `nk_window_set_focus`
- `nk_window_set_scroll`
- `nk_window_close`
- `nk_window_collapse`
- `nk_window_collapse_if`
- `nk_window_show`
- `nk_window_show_if`
- `nk_rule_horizontal`

## LAYOUT (Lines 2184 - 2819)
**Enums**: nk_widget_align, nk_widget_alignment
**Functions**: 
- `nk_layout_set_min_row_height`
- `nk_layout_reset_min_row_height`
- `nk_rect nk_layout_widget_bounds`
- `nk_layout_ratio_from_pixel`
- `nk_layout_row_dynamic`
- `nk_layout_row_static`
- `nk_layout_row_begin`
- `nk_layout_row_push`
- `nk_layout_row_end`
- `nk_layout_row`
- `nk_layout_row_template_begin`
- `nk_layout_row_template_push_dynamic`
- `nk_layout_row_template_push_variable`
- `nk_layout_row_template_push_static`
- `nk_layout_row_template_end`
- `nk_layout_space_begin`
- `nk_layout_space_push`
- `nk_layout_space_end`
- `nk_rect nk_layout_space_bounds`
- `nk_vec2 nk_layout_space_to_screen`
- `nk_vec2 nk_layout_space_to_local`
- `nk_rect nk_layout_space_rect_to_screen`
- `nk_rect nk_layout_space_rect_to_local`
- `nk_spacer`

## GROUP (Lines 2819 - 3037)
**Functions**: 
- `nk_group_begin`
- `nk_group_begin_titled`
- `nk_group_end`
- `nk_group_scrolled_offset_begin`
- `nk_group_scrolled_begin`
- `nk_group_scrolled_end`
- `nk_group_get_scroll`
- `nk_group_set_scroll`

## TREE (Lines 3037 - 3311)
**Functions**: 
- `nk_tree_push_hashed`
- `nk_tree_image_push_hashed`
- `nk_tree_pop`
- `nk_tree_state_push`
- `nk_tree_state_image_push`
- `nk_tree_state_pop`
- `nk_tree_element_push_hashed`
- `nk_tree_element_image_push_hashed`
- `nk_tree_element_pop`

## LIST VIEW (Lines 3311 - 3327)
**Structs**: nk_list_view
**Functions**: 
- `nk_list_view_begin`
- `nk_list_view_end`

## WIDGET (Lines 3327 - 3361)
**Enums**: nk_widget_layout_states, nk_widget_states
**Functions**: 
- `enum nk_widget_layout_states nk_widget`
- `enum nk_widget_layout_states nk_widget_fitting`
- `nk_rect nk_widget_bounds`
- `nk_vec2 nk_widget_position`
- `nk_vec2 nk_widget_size`
- `nk_widget_width`
- `nk_widget_height`
- `nk_widget_is_hovered`
- `nk_widget_is_mouse_clicked`
- `nk_widget_has_mouse_click_down`
- `nk_spacing`
- `nk_widget_disable_begin`
- `nk_widget_disable_end`

## TEXT (Lines 3361 - 3406)
**Enums**: nk_text_align, nk_text_alignment
**Functions**: 
- `nk_text`
- `nk_text_colored`
- `nk_text_wrap`
- `nk_text_wrap_colored`
- `nk_label`
- `nk_label_colored`
- `nk_label_wrap`
- `nk_label_colored_wrap`
- `nk_image`
- `nk_image_color`
- `nk_labelf`
- `nk_labelf_colored`
- `nk_labelf_wrap`
- `nk_labelf_colored_wrap`
- `nk_labelfv`
- `nk_labelfv_colored`
- `nk_labelfv_wrap`
- `nk_labelfv_colored_wrap`
- `nk_value_bool`
- `nk_value_`
- `nk_value_u`
- `nk_value_`
- `nk_value_color_byte`
- `nk_value_color_`
- `nk_value_color_hex`

## BUTTON (Lines 3406 - 3431)
**Functions**: 
- `nk_button_text`
- `nk_button_label`
- `nk_button_color`
- `nk_button_symbol`
- `nk_button_image`
- `nk_button_symbol_label`
- `nk_button_symbol_text`
- `nk_button_image_label`
- `nk_button_image_text`
- `nk_button_text_styled`
- `nk_button_label_styled`
- `nk_button_symbol_styled`
- `nk_button_image_styled`
- `nk_button_symbol_text_styled`
- `nk_button_symbol_label_styled`
- `nk_button_image_label_styled`
- `nk_button_image_text_styled`
- `nk_button_set_behavior`
- `nk_button_push_behavior`
- `nk_button_pop_behavior`

## CHECKBOX (Lines 3431 - 3447)
**Functions**: 
- `nk_check_label`
- `nk_check_text`
- `nk_check_text_align`
- `unsigned nk_check_flags_label`
- `unsigned nk_check_flags_text`
- `nk_checkbox_label`
- `nk_checkbox_label_align`
- `nk_checkbox_text`
- `nk_checkbox_text_align`
- `nk_checkbox_flags_label`
- `nk_checkbox_flags_text`

## RADIO BUTTON (Lines 3447 - 3460)
**Functions**: 
- `nk_radio_label`
- `nk_radio_label_align`
- `nk_radio_text`
- `nk_radio_text_align`
- `nk_option_label`
- `nk_option_label_align`
- `nk_option_text`
- `nk_option_text_align`

## SELECTABLE (Lines 3460 - 3479)
**Functions**: 
- `nk_selectable_label`
- `nk_selectable_text`
- `nk_selectable_image_label`
- `nk_selectable_image_text`
- `nk_selectable_symbol_label`
- `nk_selectable_symbol_text`
- `nk_select_label`
- `nk_select_text`
- `nk_select_image_label`
- `nk_select_image_text`
- `nk_select_symbol_label`
- `nk_select_symbol_text`

## SLIDER (Lines 3479 - 3489)
**Functions**: 
- `nk_slide_`
- `nk_slide_`
- `nk_slider_`
- `nk_slider_`

## KNOB (Lines 3489 - 3497)
**Functions**: 
- `nk_knob_`
- `nk_knob_`

## PROGRESSBAR (Lines 3497 - 3505)
**Functions**: 
- `nk_progress`
- `nk_prog`

## COLOR PICKER (Lines 3505 - 3512)
**Functions**: 
- `f _picker`
- `_pick`

## PROPERTIES (Lines 3512 - 3732)
**Functions**: 
- `nk_property_`
- `nk_property_`
- `nk_property_double`
- `nk_propertyi`
- `nk_propertyf`
- `double nk_propertyd`

## TEXT EDIT (Lines 3732 - 3770)
**Enums**: nk_edit_flags, nk_edit_types, nk_edit_events
**Functions**: 
- `nk_flags nk_edit_string`
- `nk_flags nk_edit_string_zero_terminated`
- `nk_flags nk_edit_buffer`
- `nk_edit_focus`
- `nk_edit_unfocus`

## CHART (Lines 3770 - 3784)
**Functions**: 
- `nk_chart_begin`
- `nk_chart_begin_colored`
- `nk_chart_add_slot`
- `nk_chart_add_slot_colored`
- `nk_flags nk_chart_push`
- `nk_flags nk_chart_push_slot`
- `nk_chart_end`
- `nk_plot`
- `nk_plot_function`

## POPUP (Lines 3784 - 3794)
**Functions**: 
- `nk_popup_begin`
- `nk_popup_close`
- `nk_popup_end`
- `nk_popup_get_scroll`
- `nk_popup_set_scroll`

## COMBOBOX (Lines 3794 - 3807)
**Functions**: 
- `nk_combo`
- `nk_combo_separator`
- `nk_combo_string`
- `nk_combo_callback`
- `nk_combobox`
- `nk_combobox_string`
- `nk_combobox_separator`
- `nk_combobox_callback`

## ABSTRACT COMBOBOX (Lines 3807 - 3829)
**Functions**: 
- `nk_combo_begin_text`
- `nk_combo_begin_label`
- `nk_combo_begin_color`
- `nk_combo_begin_symbol`
- `nk_combo_begin_symbol_label`
- `nk_combo_begin_symbol_text`
- `nk_combo_begin_image`
- `nk_combo_begin_image_label`
- `nk_combo_begin_image_text`
- `nk_combo_item_label`
- `nk_combo_item_text`
- `nk_combo_item_image_label`
- `nk_combo_item_image_text`
- `nk_combo_item_symbol_label`
- `nk_combo_item_symbol_text`
- `nk_combo_close`
- `nk_combo_end`

## CONTEXTUAL (Lines 3829 - 3843)
**Functions**: 
- `nk_contextual_begin`
- `nk_contextual_item_text`
- `nk_contextual_item_label`
- `nk_contextual_item_image_label`
- `nk_contextual_item_image_text`
- `nk_contextual_item_symbol_label`
- `nk_contextual_item_symbol_text`
- `nk_contextual_close`
- `nk_contextual_end`

## TOOLTIP (Lines 3843 - 3859)
**Functions**: 
- `nk_tooltip`
- `nk_tooltip_offset`
- `nk_tooltipf`
- `nk_tooltipfv`
- `nk_tooltipf_offset`
- `nk_tooltipfv_offset`
- `nk_tooltip_begin`
- `nk_tooltip_begin_offset`
- `nk_tooltip_end`

## MENU (Lines 3859 - 3882)
**Functions**: 
- `nk_menubar_begin`
- `nk_menubar_end`
- `nk_menu_begin_text`
- `nk_menu_begin_label`
- `nk_menu_begin_image`
- `nk_menu_begin_image_text`
- `nk_menu_begin_image_label`
- `nk_menu_begin_symbol`
- `nk_menu_begin_symbol_text`
- `nk_menu_begin_symbol_label`
- `nk_menu_item_text`
- `nk_menu_item_label`
- `nk_menu_item_image_label`
- `nk_menu_item_image_text`
- `nk_menu_item_symbol_text`
- `nk_menu_item_symbol_label`
- `nk_menu_close`
- `nk_menu_end`

## STYLE (Lines 3882 - 3958)
**Enums**: nk_style_colors, nk_style_cursor
**Functions**: 
- `nk_style_default`
- `nk_style_from_table`
- `nk_style_load_cursor`
- `nk_style_load_all_cursors`
- `const char nk_style_get_color_by_name`
- `nk_style_set_font`
- `nk_style_set_cursor`
- `nk_style_show_cursor`
- `nk_style_hide_cursor`
- `nk_style_push_font`
- `nk_style_push_`
- `nk_style_push_vec2`
- `nk_style_push_style_item`
- `nk_style_push_flags`
- `nk_style_push_color`
- `nk_style_pop_font`
- `nk_style_pop_`
- `nk_style_pop_vec2`
- `nk_style_pop_style_item`
- `nk_style_pop_flags`
- `nk_style_pop_color`

## COLOR (Lines 3958 - 4022)
**Functions**: 
- `nk_rgb`
- `nk_rgb_iv`
- `nk_rgb_bv`
- `nk_rgb_f`
- `nk_rgb_fv`
- `nk_rgb_cf`
- `nk_rgb_hex`
- `nk_rgb_factor`
- `nk_rgba`
- `nk_rgba_u32`
- `nk_rgba_iv`
- `nk_rgba_bv`
- `nk_rgba_f`
- `nk_rgba_fv`
- `nk_rgba_cf`
- `nk_rgba_hex`
- `f nk_hsva_colorf`
- `f nk_hsva_colorfv`
- `f_hsva_f`
- `f_hsva_fv`
- `nk_hsv`
- `nk_hsv_iv`
- `nk_hsv_bv`
- `nk_hsv_f`
- `nk_hsv_fv`
- `nk_hsva`
- `nk_hsva_iv`
- `nk_hsva_bv`
- `nk_hsva_f`
- `nk_hsva_fv`
- `_f`
- `_fv`
- `f _cf`
- `_d`
- `_dv`
- `_u32`
- `_hex_rgba`
- `_hex_rgb`
- `_hsv_i`
- `_hsv_b`
- `_hsv_iv`
- `_hsv_bv`
- `_hsv_f`
- `_hsv_fv`
- `_hsva_i`
- `_hsva_b`
- `_hsva_iv`
- `_hsva_bv`
- `_hsva_f`
- `_hsva_fv`

## IMAGE (Lines 4022 - 4036)
**Functions**: 
- `_ptr`
- `_id`
- `nk_image nk_image_handle`
- `nk_image nk_image_ptr`
- `nk_image nk_image_id`
- `nk_image_is_subimage`
- `nk_image nk_subimage_ptr`
- `nk_image nk_subimage_id`
- `nk_image nk_subimage_handle`

## 9-SLICE (Lines 4036 - 4048)
**Functions**: 
- `nk_nine_slice nk_nine_slice_handle`
- `nk_nine_slice nk_nine_slice_ptr`
- `nk_nine_slice nk_nine_slice_id`
- `nk_nine_slice_is_sub9slice`
- `nk_nine_slice nk_sub9slice_ptr`
- `nk_nine_slice nk_sub9slice_id`
- `nk_nine_slice nk_sub9slice_handle`

## MATH (Lines 4048 - 4069)
**Functions**: 
- `nk_murmur_hash`
- `nk_triangle_from_direction`
- `nk_vec2 nk_vec2`
- `nk_vec2 nk_vec2i`
- `nk_vec2 nk_vec2v`
- `nk_vec2 nk_vec2iv`
- `nk_rect nk_get_null_rect`
- `nk_rect nk_rect`
- `nk_rect nk_recti`
- `nk_rect nk_recta`
- `nk_rect nk_rectv`
- `nk_rect nk_rectiv`
- `nk_vec2 nk_rect_pos`
- `nk_vec2 nk_rect_size`

## STRING (Lines 4069 - 4087)
**Functions**: 
- `nk_strlen`
- `nk_stricmp`
- `nk_stricmpn`
- `nk_strtoi`
- `nk_strtof`
- `double nk_strtod`
- `nk_strfilter`
- `nk_strmatch_fuzzy_string`
- `nk_strmatch_fuzzy_text`

## UTF-8 (Lines 4087 - 4096)
**Functions**: 
- `nk_utf_decode`
- `nk_utf_encode`
- `nk_utf_len`
- `const char nk_utf_at`

## FONT (Lines 4096 - 4389)
**Structs**: nk_user_font_glyph, nk_user_font, nk_baked_font, nk_font_config, nk_font_glyph, nk_font, nk_font_atlas
**Enums**: nk_font_coord_type, nk_font_atlas_format
**Functions**: 
- `const nk_rune nk_font_default_glyph_ranges`
- `const nk_rune nk_font_chinese_glyph_ranges`
- `const nk_rune nk_font_cyrillic_glyph_ranges`
- `const nk_rune nk_font_korean_glyph_ranges`
- `nk_font_atlas_init_default`
- `nk_font_atlas_init`
- `nk_font_atlas_init_custom`
- `nk_font_atlas_begin`
- `nk_font_config nk_font_config`
- `nk_font nk_font_atlas_add`
- `nk_font nk_font_atlas_add_default`
- `nk_font nk_font_atlas_add_from_memory`
- `nk_font nk_font_atlas_add_from_file`
- `nk_font nk_font_atlas_add_compressed`
- `nk_font nk_font_atlas_add_compressed_base85`
- `const  nk_font_atlas_bake`
- `nk_font_atlas_end`
- `const  nk_font_glyph nk_font_find_glyph`
- `nk_font_atlas_cleanup`
- `nk_font_atlas_clear`

## MEMORY BUFFER (Lines 4389 - 4478)
**Structs**: nk_memory_status, nk_buffer_marker, nk_memory, nk_buffer
**Enums**: nk_allocation_type, nk_buffer_allocation_type
**Functions**: 
- `nk_buffer_init_default`
- `nk_buffer_init`
- `nk_buffer_init_fixed`
- `nk_buffer_info`
- `nk_buffer_push`
- `nk_buffer_mark`
- `nk_buffer_reset`
- `nk_buffer_clear`
- `nk_buffer_free`
- `nk_buffer_memory`
- `const  nk_buffer_memory_const`
- `nk_buffer_total`

## STRING (Lines 4478 - 4534)
**Structs**: nk_str
**Functions**: 
- `nk_str_init_default`
- `nk_str_init`
- `nk_str_init_fixed`
- `nk_str_clear`
- `nk_str_free`
- `nk_str_append_text_char`
- `nk_str_append_str_char`
- `nk_str_append_text_utf8`
- `nk_str_append_str_utf8`
- `nk_str_append_text_runes`
- `nk_str_append_str_runes`
- `nk_str_insert_at_char`
- `nk_str_insert_at_rune`
- `nk_str_insert_text_char`
- `nk_str_insert_str_char`
- `nk_str_insert_text_utf8`
- `nk_str_insert_str_utf8`
- `nk_str_insert_text_runes`
- `nk_str_insert_str_runes`
- `nk_str_remove_chars`
- `nk_str_remove_runes`
- `nk_str_delete_chars`
- `nk_str_delete_runes`
- `char nk_str_at_char`
- `char nk_str_at_rune`
- `nk_rune nk_str_rune_at`
- `const char nk_str_at_char_const`
- `const char nk_str_at_const`
- `char nk_str_get`
- `const char nk_str_get_const`
- `nk_str_len`
- `nk_str_len_char`

## TEXT EDITOR (Lines 4534 - 4652)
**Structs**: nk_clipboard, nk_text_undo_record, nk_text_undo_state, nk_text_edit
**Enums**: nk_text_edit_type, nk_text_edit_mode
**Functions**: 
- `nk_filter_default`
- `nk_filter_ascii`
- `nk_filter_`
- `nk_filter_decimal`
- `nk_filter_hex`
- `nk_filter_oct`
- `nk_filter_binary`
- `nk_textedit_init_default`
- `nk_textedit_init`
- `nk_textedit_init_fixed`
- `nk_textedit_free`
- `nk_textedit_text`
- `nk_textedit_delete`
- `nk_textedit_delete_selection`
- `nk_textedit_select_all`
- `nk_textedit_cut`
- `nk_textedit_paste`
- `nk_textedit_undo`
- `nk_textedit_redo`

## DRAWING (Lines 4652 - 4927)
**Structs**: nk_command, nk_command_scissor, nk_command_line, nk_command_curve, nk_command_rect, nk_command_rect_filled, nk_command_rect_multi_color, nk_command_triangle, nk_command_triangle_filled, nk_command_circle, nk_command_circle_filled, nk_command_arc, nk_command_arc_filled, nk_command_polygon, nk_command_polygon_filled, nk_command_polyline, nk_command_image, nk_command_custom, nk_command_text, nk_command_buffer
**Enums**: nk_command_type, nk_command_clipping
**Functions**: 
- `nk_stroke_line`
- `nk_stroke_curve`
- `nk_stroke_rect`
- `nk_stroke_circle`
- `nk_stroke_arc`
- `nk_stroke_triangle`
- `nk_stroke_polyline`
- `nk_stroke_polygon`
- `nk_fill_rect`
- `nk_fill_rect_multi_color`
- `nk_fill_circle`
- `nk_fill_arc`
- `nk_fill_triangle`
- `nk_fill_polygon`
- `nk_draw_image`
- `nk_draw_nine_slice`
- `nk_draw_text`
- `nk_push_scissor`
- `nk_push_custom`

## INPUT (Lines 4927 - 4984)
**Structs**: nk_mouse_button, nk_mouse, nk_key, nk_keyboard, nk_input
**Functions**: 
- `nk_input_has_mouse_click`
- `nk_input_has_mouse_click_in_rect`
- `nk_input_has_mouse_click_in_button_rect`
- `nk_input_has_mouse_click_down_in_rect`
- `nk_input_is_mouse_click_in_rect`
- `nk_input_is_mouse_click_down_in_rect`
- `nk_input_any_mouse_click_in_rect`
- `nk_input_is_mouse_prev_hovering_rect`
- `nk_input_is_mouse_hovering_rect`
- `nk_input_is_mouse_moved`
- `nk_input_mouse_clicked`
- `nk_input_is_mouse_down`
- `nk_input_is_mouse_pressed`
- `nk_input_is_mouse_released`
- `nk_input_is_key_pressed`
- `nk_input_is_key_released`
- `nk_input_is_key_down`

## DRAW LIST (Lines 4984 - 5136)
**Structs**: nk_draw_vertex_layout_element, nk_draw_command, nk_draw_list
**Enums**: nk_draw_list_stroke, nk_draw_vertex_layout_attribute, nk_draw_vertex_layout_format
**Functions**: 
- `nk_draw_list_init`
- `nk_draw_list_setup`
- `const  nk_draw_command nk__draw_list_begin`
- `const  nk_draw_command nk__draw_list_next`
- `const  nk_draw_command nk__draw_list_end`
- `nk_draw_list_path_clear`
- `nk_draw_list_path_line_to`
- `nk_draw_list_path_arc_to_fast`
- `nk_draw_list_path_arc_to`
- `nk_draw_list_path_rect_to`
- `nk_draw_list_path_curve_to`
- `nk_draw_list_path_fill`
- `nk_draw_list_path_stroke`
- `nk_draw_list_stroke_line`
- `nk_draw_list_stroke_rect`
- `nk_draw_list_stroke_triangle`
- `nk_draw_list_stroke_circle`
- `nk_draw_list_stroke_curve`
- `nk_draw_list_stroke_poly_line`
- `nk_draw_list_fill_rect`
- `nk_draw_list_fill_rect_multi_color`
- `nk_draw_list_fill_triangle`
- `nk_draw_list_fill_circle`
- `nk_draw_list_fill_poly_convex`
- `nk_draw_list_add_image`
- `nk_draw_list_add_text`
- `nk_draw_list_push_userdata`

## GUI (Lines 5136 - 5644)
**Structs**: nk_style_item, nk_style_text, nk_style_button, nk_style_toggle, nk_style_selectable, nk_style_slider, nk_style_knob, nk_style_progress, nk_style_scrollbar, nk_style_edit, nk_style_property, nk_style_chart, nk_style_combo, nk_style_tab, nk_style_window_header, nk_style_window, nk_style
**Enums**: nk_style_item_type, nk_style_header_align
**Functions**: 
- `nk_style_item nk_style_item_color`
- `nk_style_item nk_style_item_image`
- `nk_style_item nk_style_item_nine_slice`
- `nk_style_item nk_style_item_hide`

## PANEL (Lines 5644 - 5747)
**Structs**: nk_chart_slot, nk_chart, nk_row_layout, nk_popup_buffer, nk_menu_state, nk_panel
**Enums**: nk_panel_type, nk_panel_set, nk_panel_row_layout_type

## WINDOW (Lines 5747 - 5836)
**Structs**: nk_popup_state, nk_edit_state, nk_property_state, nk_window
**Enums**: nk_window_flags

## STACK (Lines 5836 - 5930)
**Structs**: nk_config_stack_##name##_element, nk_config_stack_##type, nk_configuration_stacks

## CONTEXT (Lines 5930 - 6014)
**Structs**: nk_table, nk_page_element, nk_page, nk_pool, nk_context

## MATH (Lines 6014 - 6044)

## ALIGNMENT (Lines 6044 - 6494)
**Structs**: nk_text, nk_property_variant
**Enums**: nk_window_insert_location, nk_toggle_type, nk_property_status, nk_property_filter, nk_property_kind

## MATH (Lines 6494 - 6850)

## UTIL (Lines 6850 - 7978)
**Enums**: nk_arg_type, nk_arg_flags

## COLOR (Lines 7978 - 8402)
**Structs**: nk_colorf

## UTF-8 (Lines 8402 - 8547)

## BUFFER (Lines 8547 - 8824)

## STRING (Lines 8824 - 9273)

## DRAW (Lines 9273 - 9831)

## VERTEX (Lines 9831 - 16884)

## TRUETYPE (Lines 16884 - 18247)
**Structs**: nk_font_bake_data, nk_font_baker

## INPUT (Lines 18247 - 18538)

## STYLE (Lines 18538 - 19411)
**Functions**: 
- `nk_style_default`
- `NK_STYLE_PUSH_IMPLEMENATION`
- `NK_STYLE_PUSH_IMPLEMENATION`
- `NK_STYLE_PUSH_IMPLEMENATION`
- `NK_STYLE_PUSH_IMPLEMENATION`
- `NK_STYLE_PUSH_IMPLEMENATION`
- `NK_STYLE_POP_IMPLEMENATION`
- `NK_STYLE_POP_IMPLEMENATION`
- `NK_STYLE_POP_IMPLEMENATION`
- `NK_STYLE_POP_IMPLEMENATION`
- `NK_STYLE_POP_IMPLEMENATION`

## CONTEXT (Lines 19411 - 19756)

## POOL (Lines 19756 - 19823)

## PAGE ELEMENT (Lines 19823 - 19886)

## TABLE (Lines 19886 - 19976)

## PANEL (Lines 19976 - 20598)
**Structs**: nk_rect

## WINDOW (Lines 20598 - 21279)

## POPUP (Lines 21279 - 21543)

## CONTEXTUAL (Lines 21543 - 21770)
**Structs**: nk_rect

## MENU (Lines 21770 - 22068)
**Functions**: 
- `nk_menu_begin_label`
- `nk_menu_item_symbol_text`
- `nk_menu_item_symbol_label`
- `nk_menu_close`

## LAYOUT (Lines 22068 - 22837)
**Structs**: nk_rect

## TREE (Lines 22837 - 23189)
**Structs**: nk_rect, nk_rect, nk_rect, nk_rect

## GROUP (Lines 23189 - 23426)

## LIST VIEW (Lines 23426 - 23509)

## WIDGET (Lines 23509 - 23870)

## TEXT (Lines 23870 - 24170)

## IMAGE (Lines 24170 - 24310)

## 9-SLICE (Lines 24310 - 24417)

## BUTTON (Lines 24417 - 25110)
**Structs**: nk_rect
**Functions**: 
- `nk_button_label_styled`
- `nk_button_label`
- `nk_button_symbol_label`
- `nk_button_symbol_label_styled`
- `nk_button_image_label`
- `nk_button_image_label_styled`

## TOGGLE (Lines 25110 - 25550)
**Functions**: 
- `nk_check_label`
- `unsigned  nk_check_flags_label`
- `nk_checkbox_label`
- `nk_checkbox_label_align`
- `nk_checkbox_flags_label`

## SELECTABLE (Lines 25550 - 25883)
**Functions**: 
- `nk_select_text`
- `nk_selectable_label`
- `nk_selectable_image_label`
- `nk_select_label`
- `nk_select_image_label`
- `nk_select_image_text`

## SLIDER (Lines 25883 - 26146)

## KNOB (Lines 26146 - 26399)

## PROGRESS (Lines 26399 - 26558)

## SCROLLBAR (Lines 26558 - 26869)

## TEXT EDITOR (Lines 26869 - 27905)
**Structs**: nk_text_find, nk_text_edit_row

## FILTER (Lines 27905 - 27967)

## EDIT (Lines 27967 - 28742)

## PROPERTY (Lines 28742 - 29285)

## CHART (Lines 29285 - 29621)
**Structs**: nk_rect, nk_rect

## COLOR PICKER (Lines 29621 - 29823)

## COMBO (Lines 29823 - 30679)
**Structs**: nk_rect, nk_rect
**Functions**: 
- `nk_combo_end`
- `nk_combo_close`

## TOOLTIP (Lines 30679 - 31297)

