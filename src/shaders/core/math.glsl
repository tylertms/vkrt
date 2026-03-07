#ifndef SHADER_MATH_GLSL
#define SHADER_MATH_GLSL

#define PI 3.1415926535897932384626433832
#define INV_PI 0.318309886183790671537767

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

mat3 buildTBN(vec3 normal) {
    vec3 bitangent = normalize(cross(normal, abs(normal.x) > 0.5 ? vec3(0,1,0) : vec3(1,0,0)));
    vec3 tangent = cross(bitangent, normal);
    return mat3(tangent, bitangent, normal);
}

#endif
