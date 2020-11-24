#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform buffer_object {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 v_normal;

void main() {
    mat4 vm = ubo.view * ubo.model;
    gl_Position = ubo.proj * vm * vec4(pos, 1.0);
    v_normal = transpose(inverse(mat3(vm))) * normal;
}
