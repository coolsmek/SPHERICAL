#version 450
layout(set = 0, binding = 0) uniform sampler2D fontAtlas;
layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;
void main() {
    // R8 atlas: red channel = glyph alpha / white solid pixel for solid fills
    float alpha = texture(fontAtlas, inUV).r;
    outColor = inColor * vec4(1.0, 1.0, 1.0, alpha);
}