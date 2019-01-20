#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 col_frag;

layout(location = 0) out vec4 col_out;

void main() {
    col_out = vec4(col_frag, 1.0);
}
