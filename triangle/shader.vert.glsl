#version 450    
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform buffer_object {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec2 pos;
layout(location = 1) in vec4 col;

layout(location = 0) out vec4 col_frag;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(pos, 0.0, 1.0);
    col_frag = col;
}
