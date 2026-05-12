#version 450
layout(set = 0, binding = 0) uniform sampler2D fontAtlas;
layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;

const float kMsdfPxRange = 5.0;

float median3(float a, float b, float c) {
    return max(min(a, b), min(max(a, b), c));
}

float screenPxRange() {
    vec2 unitRange = vec2(kMsdfPxRange) / vec2(textureSize(fontAtlas, 0));
    vec2 screenTexSize = vec2(1.0) / fwidth(inUV);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main() {
    vec3 msdf = texture(fontAtlas, inUV).rgb;
    float sdf = median3(msdf.r, msdf.g, msdf.b) - 0.5;
    float screenPxDistance = screenPxRange() * sdf;
    float alpha = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    outColor = inColor * vec4(1.0, 1.0, 1.0, alpha);
}