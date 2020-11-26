#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_texture;

layout(location = 0) out vec4 col_out;

layout(binding = 1) uniform sampler2D tex;

const vec3 SOURCE = vec3(0.0, 0.0, 1.0);

void main() {
    float brightness = dot(normalize(v_normal), normalize(SOURCE));
    vec4 color = texture(tex, v_texture);
    col_out = vec4(mix(0.6*vec3(color), vec3(color), brightness), color[3]);
}
