#pragma once

// =============================================================================
// Nuklear compile-time configuration for SPHERICAL SDK
// Include this header BEFORE any #include <nuklear.h> across the SDK.
// =============================================================================

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT   // enables nk_convert(), nk_draw_foreach, glyph query
#define NK_INCLUDE_COMMAND_USERDATA

struct SphericalNkVertex {
	float position[2];
	float uv[2];
	unsigned char col[4];
};



