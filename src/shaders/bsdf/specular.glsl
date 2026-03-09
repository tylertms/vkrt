#ifndef SHADER_BSDF_SPECULAR_GLSL
#define SHADER_BSDF_SPECULAR_GLSL

float GTR1(float NdotH, float a) {
    if (a >= 1.0) return INV_PI;
    float a2 = a * a;
    float t = 1.0 + (a2 - 1.0) * NdotH * NdotH;
    return (a2 - 1.0) / (PI * log(a2) * t);
}

float GTR2Aniso(float NdotH, float HdotX, float HdotY, float ax, float ay) {
    float sx = HdotX / ax;
    float sy = HdotY / ay;
    float denom = sx * sx + sy * sy + NdotH * NdotH;
    return 1.0 / (PI * ax * ay * denom * denom);
}

float smithGGX(float NdotV, float alphaG) {
    float a = alphaG * alphaG;
    float b = NdotV * NdotV;
    return 1.0 / (NdotV + sqrt(max(a + b - a * b, 0.0)));
}

float smithGGXCorrelatedVisibilityAniso(
    float NdotL,
    float NdotV,
    float LdotX,
    float LdotY,
    float VdotX,
    float VdotY,
    float ax,
    float ay
) {
    float lambdaV = NdotL * length(vec3(ax * VdotX, ay * VdotY, NdotV));
    float lambdaL = NdotV * length(vec3(ax * LdotX, ay * LdotY, NdotL));
    return 0.5 / max(lambdaV + lambdaL, 1e-6);
}

float boundedVNDFCapK(vec3 localV, float ax, float ay) {
    float a = min(min(ax, ay), 1.0);
    float s = 1.0 + length(localV.xy);
    float a2 = a * a;
    float s2 = s * s;
    return (1.0 - a2) * s2 / max(s2 + a2 * localV.z * localV.z, 1e-6);
}

float pdfGGXBoundedVNDF(mat3 basis, vec3 incoming, vec3 outgoing, float ax, float ay) {
    float NdotL = dot(basis[2], incoming);
    float NdotV = dot(basis[2], outgoing);
    if (NdotL <= 0.0 || NdotV <= 0.0) return 0.0;

    vec3 H = normalize(incoming + outgoing);
    float NdotH = max(dot(basis[2], H), 0.0);
    float LdotH = max(dot(incoming, H), 0.0);
    if (NdotH <= 0.0 || LdotH <= 0.0) return 0.0;

    vec3 localV = vec3(dot(outgoing, basis[0]), dot(outgoing, basis[1]), NdotV);
    float HdotX = dot(H, basis[0]);
    float HdotY = dot(H, basis[1]);
    float Ds = GTR2Aniso(NdotH, HdotX, HdotY, ax, ay);

    vec2 ai = vec2(ax * localV.x, ay * localV.y);
    float len2 = dot(ai, ai);
    float t = sqrt(len2 + localV.z * localV.z);
    if (localV.z >= 0.0) {
        float k = boundedVNDFCapK(localV, ax, ay);
        return Ds / max(2.0 * (k * localV.z + t), 1e-6);
    }
    return Ds * (t - localV.z) / max(2.0 * len2, 1e-6);
}

vec3 sampleGGXBoundedVNDF(mat3 basis, vec3 outgoing, float ax, float ay, inout uint state) {
    vec3 localV = vec3(dot(outgoing, basis[0]), dot(outgoing, basis[1]), dot(outgoing, basis[2]));
    vec3 stretchedV = normalize(vec3(localV.x * ax, localV.y * ay, localV.z));

    float phi = TWO_PI * rand(state);
    float k = boundedVNDFCapK(localV, ax, ay);
    float b = localV.z > 0.0 ? k * stretchedV.z : stretchedV.z;
    float z = 1.0 - rand(state) * (1.0 + b);
    float sinTheta = sqrt(max(1.0 - z * z, 0.0));
    vec3 localOStd = vec3(sinTheta * cos(phi), sinTheta * sin(phi), z);
    vec3 localMStd = stretchedV + localOStd;
    vec3 localM = normalize(vec3(localMStd.x * ax, localMStd.y * ay, localMStd.z));
    vec3 localL = reflect(-localV, localM);
    return normalize(basis * localL);
}

vec3 sampleClearcoatHalfVector(mat3 basis, float alpha, inout uint state) {
    float u1 = rand(state);
    float u2 = rand(state);
    float phi = TWO_PI * u2;
    float a2 = alpha * alpha;
    float cosTheta = sqrt(max((1.0 - pow(a2, 1.0 - u1)) / max(1.0 - a2, 1e-6), 0.0));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    return normalize(
        basis[0] * (sinTheta * cos(phi)) +
            basis[1] * (sinTheta * sin(phi)) +
            basis[2] * cosTheta
    );
}

float ggxDirectionalAlbedoFit(float NdotV, float ax, float ay) {
    float alphaUV = clamp(ax * ay, 0.0, 1.0);
    float mu = clamp(NdotV, 0.0, 1.0);
    float viewPoly = 3.09507 + mu * (-9.11369 + mu * (15.8884 + mu * (-13.70343 + 4.51786 * mu)));
    float roughPoly = -0.20277 + alphaUV * (2.772 + alphaUV * (-2.6175 + 0.73343 * alphaUV));
    float Ess = 1.0 - 1.4594 * alphaUV * mu * roughPoly * viewPoly;
    return clamp(Ess, 1e-3, 1.0);
}

vec3 applyGGXMultiScatter(vec3 specular, vec3 spec0, float NdotV, float ax, float ay) {
    float Ess = ggxDirectionalAlbedoFit(NdotV, ax, ay);
    return specular * (1.0 + spec0 * (1.0 / Ess - 1.0));
}

#endif
