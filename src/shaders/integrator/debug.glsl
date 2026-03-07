#ifndef SHADER_DEBUG_GLSL
#define SHADER_DEBUG_GLSL

vec3 debugBounceHeatmap(uint bounces, uint rrMaxDepth) {
    float t = clamp(float(bounces) / float(rrMaxDepth), 0.0, 1.0);
    if (t < 0.5) return mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), t * 2.0);
    return mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t - 0.5) * 2.0);
}

float debugDepthValue(float distance) {
    float scaledDistance = max(distance, 0.0) * 0.25;
    return 1.0 / (1.0 + scaledDistance);
}

#endif
