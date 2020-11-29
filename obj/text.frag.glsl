#version 450

layout(location = 0) in vec2 v_texture;
layout(location = 0) out vec4 col_out;

layout(binding = 0) uniform sampler2D font;

void main() {
    col_out = vec4(texture(font, v_texture).r);
}
