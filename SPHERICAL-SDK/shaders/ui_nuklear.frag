#version 450
layout(set = 0, binding = 0) uniform sampler2D fontAtlas;
layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;
void main() {
    // R8 SDF atlas: red channel stores signed-distance field.
    // 0.5 = glyph edge; >0.5 = inside; <0.5 = outside.
    // Thresholds tuned for 64px bake -> 24px display (scale ~0.375).
    // Too tight (0.485/0.515) = hard threshold, breaks thin strokes at sub-pixel.
    // Too wide (0.42/0.58)    = soft halo/fringe visible around glyphs.
    float sdf = texture(fontAtlas, inUV).r;
    float alpha = smoothstep(0.45, 0.55, sdf);
    outColor = inColor * vec4(1.0, 1.0, 1.0, alpha);
}

