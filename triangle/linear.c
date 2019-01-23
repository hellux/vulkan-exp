#include "linear.h"

#include <math.h>

void cross(vec3 product, vec3 a, vec3 b) {
    product[0] = a[1]*b[2] - a[2]*b[1];
    product[1] = a[2]*b[0] - a[0]*b[2];
    product[2] = a[0]*b[1] - a[1]*b[0];
}

void subtract(vec3 c, vec3 a, vec3 b) {
    c[0] = a[0]-b[0];
    c[1] = a[1]-b[1];
    c[2] = a[2]-b[2];
}

float dot(vec3 a, vec3 b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

void normalize(vec3 normal, vec3 vec) {
    float length = sqrt(dot(vec, vec));
    normal[0] = vec[0]/length;
    normal[1] = vec[1]/length;
    normal[2] = vec[2]/length;
}

void look_at(mat4 mat, vec3 eye, vec3 center, vec3 up) {
    vec3 e, f;
    subtract(e, center, eye);
    normalize(f, e);

    vec3 r, s;
    cross(r, f, up);
    normalize(s, r);

    vec3 u;
    cross(u, s, f);

    mat[0][0] = s[0];
    mat[0][1] = u[0];
    mat[0][2] =-f[0];
    mat[1][0] = s[1];
    mat[1][1] = u[1];
    mat[1][2] =-f[1];
    mat[2][1] = u[2];
    mat[2][0] = s[2];
    mat[2][2] =-f[2];
    mat[3][0] =-dot(s, eye);
    mat[3][1] =-dot(u, eye);
    mat[3][2] = dot(f, eye);
}

void perspective(mat4 mat, float fov, float aspect,
                           float near, float far) {
    mat[0][0] = 1 / aspect / tan(fov/2);
    mat[0][1] = 0;
    mat[0][2] = 0;
    mat[0][3] = 0;
    mat[1][0] = 0;
    mat[1][1] = -1 / tan(fov/2);
    mat[1][2] = 0;
    mat[1][3] = 0;
    mat[2][0] = 0;
    mat[2][1] = 0;
    mat[2][2] = (far + near) / (near - far);
    mat[2][3] = -1;
    mat[3][0] = 0;
    mat[3][1] = 0;
    mat[3][2] = 2*(far * near) / (near - far);
    mat[3][3] = 0;
}
