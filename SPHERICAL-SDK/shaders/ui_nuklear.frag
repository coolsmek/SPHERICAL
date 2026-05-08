#version 450
layout(set = 0, binding = 0) uniform sampler2D fontAtlas;
layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;
void main() {
    // R8 SDF atlas: red channel stores signed-distance field.
    // FreeType SDF encoding: 0.502 (128/255) = glyph edge, >0.502 = inside, <0.502 = outside.
    //
    // Use screen-space derivative-based edge width instead of a fixed band.
    // Fixed bands like smoothstep(0.45, 0.55) cause "static" noise inside complex
    // glyphs (a, e, B, R, etc.) because FreeType SDF values near counter-curves can
    // oscillate slightly around 0.5, falling in the partial-transparency range.
    //
    // With the derivative approach:
    //   - Deep inside glyph: SDF gradient is tiny -> edgeWidth ~ 0 -> alpha snaps to 1.0
    //   - At the glyph edge: gradient is maximum -> edgeWidth is proportional to pixel -> smooth AA
    //   - Deep outside: gradient is tiny -> alpha snaps to 0.0
    // This eliminates interior artifacts at all display scales.
    float sdf = texture(fontAtlas, inUV).r;
    float df = sdf - 0.503;
    float edgeWidth = max(0.0001, fwidth(sdf) * 0.85); //was 0.7
    float alpha = clamp(df / edgeWidth + 0.5, 0.0, 1.0);
    outColor = inColor * vec4(1.0, 1.0, 1.0, alpha);
}