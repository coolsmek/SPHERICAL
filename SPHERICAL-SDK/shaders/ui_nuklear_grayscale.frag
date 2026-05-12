#version 450
layout(set = 0, binding = 0) uniform sampler2D fontAtlas;
layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;
void main() {
    // R8 grayscale atlas: red channel stores pre-hinted glyph coverage.
    float coverage = texture(fontAtlas, inUV).r;

    // Tighten coverage slightly so small hinted text looks darker and crisper,
    // closer to classic Windows UI text rather than soft billboard text.
    coverage = smoothstep(0.035, 0.965, coverage);
    coverage = pow(coverage, 0.90);

    vec3 encodedColor = pow(inColor.rgb, vec3(1.0 / 2.2));
    outColor = vec4(encodedColor, inColor.a * coverage);
}