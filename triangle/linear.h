typedef float mat4[4][4];
typedef float vec2[2];
typedef float vec4[4];
typedef float vec3[3];

void perspective(mat4 mat, float fov, float aspect,
                           float near, float far);
void look_at(mat4 mat, vec3 eye, vec3 center, vec3 up);
