#version 450

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 texture;

layout(location = 0) out vec2 v_texture;

void main() {
    gl_Position = vec4(pos, 1.0, 1.0);
    v_texture = texture;
}
