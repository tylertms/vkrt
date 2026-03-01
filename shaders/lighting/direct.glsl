#ifndef SHADER_DIRECT_LIGHT_GLSL
#define SHADER_DIRECT_LIGHT_GLSL

#include "utility/bindings.glsl"
#include "utility/rand.glsl"
#include "utility/timeline.glsl"

struct DirectLightSample {
    vec3 wi;
    vec3 radiance;
    float distance;
    float pdf;
};

uint findEmissiveMeshRecord(uint meshIndex) {
    uint lo = 0u;
    uint hi = scene.emissiveMeshCount;
    while (lo < hi) {
        uint mid = lo + (hi - lo) / 2u;
        uint candidate = emissiveMeshBuffer.meshes[mid].indices.x;
        if (candidate < meshIndex) lo = mid + 1u;
        else hi = mid;
    }

    if (lo < scene.emissiveMeshCount && emissiveMeshBuffer.meshes[lo].indices.x == meshIndex) {
        return lo;
    }
    return 0xFFFFFFFFu;
}

uint sampleEmissiveMesh(inout uint state) {
    float choose = rand(state);
    uint lo = 0u;
    uint hi = scene.emissiveMeshCount;
    while (lo < hi) {
        uint mid = lo + (hi - lo) / 2u;
        if (choose <= emissiveMeshBuffer.meshes[mid].stats.x) hi = mid;
        else lo = mid + 1u;
    }
    return min(lo, scene.emissiveMeshCount - 1u);
}

vec3 emissiveRadiance(EmissiveMesh light, bool timelineActive, float observationTime, float eventTime) {
    vec3 emissionColor = light.emission.rgb;
    float emissionStrength = light.emission.a;
    if (timelineActive && emissionStrength > 0.0) {
        TimelineSample timelineValue = sampleTimeline(observationTime - eventTime);
        emissionStrength *= max(timelineValue.emissionScale, 0.0);
        emissionColor *= max(timelineValue.emissionTint, vec3(0.0));
    }
    return emissionColor * emissionStrength;
}

bool sampleDirectLight(
    vec3 shadingPoint,
    float currentPathTime,
    float observationTime,
    bool timelineActive,
    inout uint state,
    out DirectLightSample outSample
) {
    outSample.wi = vec3(0.0);
    outSample.radiance = vec3(0.0);
    outSample.distance = 0.0;
    outSample.pdf = 0.0;

    if (scene.emissiveMeshCount == 0u || scene.emissiveTriangleCount == 0u) {
        return false;
    }

    uint lightIndex = sampleEmissiveMesh(state);

    EmissiveMesh light = emissiveMeshBuffer.meshes[lightIndex];
    uint triangleCount = light.indices.z;
    if (triangleCount == 0u) return false;
    float totalArea = light.stats.y;
    if (!(totalArea > 0.0)) return false;

    float chooseArea = rand(state) * totalArea;
    uint lo = 0u;
    uint hi = triangleCount;
    while (lo < hi) {
        uint mid = lo + (hi - lo) / 2u;
        uint midIndex = light.indices.y + mid;
        float cdf = emissiveTriangleBuffer.triangles[midIndex].e1Pad.w;
        if (chooseArea <= cdf) hi = mid;
        else lo = mid + 1u;
    }

    uint localTri = min(lo, triangleCount - 1u);
    uint triIndex = light.indices.y + localTri;
    if (triIndex >= scene.emissiveTriangleCount) return false;

    EmissiveTriangle tri = emissiveTriangleBuffer.triangles[triIndex];
    float area = tri.v0Area.w;
    if (!(area > 0.0)) return false;

    float r1 = rand(state);
    float r2 = rand(state);
    float sqrtR1 = sqrt(max(r1, 0.0));
    float b1 = sqrtR1 * (1.0 - r2);
    float b2 = sqrtR1 * r2;

    vec3 v0 = tri.v0Area.xyz;
    vec3 e1 = tri.e1Pad.xyz;
    vec3 e2 = tri.e2Pad.xyz;
    vec3 lightPoint = v0 + e1 * b1 + e2 * b2;
    vec3 lightNormal = normalize(cross(e1, e2));

    vec3 toLight = lightPoint - shadingPoint;
    float dist2 = dot(toLight, toLight);
    if (!(dist2 > 1e-8)) return false;
    float dist = sqrt(dist2);
    vec3 wi = toLight / dist;

    float cosLight = max(dot(lightNormal, -wi), 0.0);
    if (cosLight <= 1e-7) return false;

    float pSelectMesh = light.stats.z;
    float pArea = pSelectMesh / totalArea;
    float pOmega = pArea * dist2 / max(cosLight, 1e-6);
    if (!(pOmega > 1e-8)) return false;

    outSample.wi = wi;
    outSample.distance = dist;
    outSample.pdf = pOmega;
    outSample.radiance = emissiveRadiance(light, timelineActive, observationTime, currentPathTime + dist);
    return true;
}

float lightPdfForEmitterHit(uint meshIndex, uint primitiveIndex, vec3 previousPoint, vec3 hitPoint) {
    if (scene.emissiveMeshCount == 0u || scene.emissiveTriangleCount == 0u) return 0.0;

    uint lightIndex = findEmissiveMeshRecord(meshIndex);
    if (lightIndex == 0xFFFFFFFFu) return 0.0;

    EmissiveMesh light = emissiveMeshBuffer.meshes[lightIndex];
    if (primitiveIndex >= light.indices.z || light.indices.z == 0u) return 0.0;
    if (!(light.stats.y > 0.0) || !(light.stats.z > 0.0)) return 0.0;

    uint triIndex = light.indices.y + primitiveIndex;
    if (triIndex >= scene.emissiveTriangleCount) return 0.0;

    EmissiveTriangle tri = emissiveTriangleBuffer.triangles[triIndex];
    float area = tri.v0Area.w;
    if (!(area > 0.0)) return 0.0;

    vec3 e1 = tri.e1Pad.xyz;
    vec3 e2 = tri.e2Pad.xyz;
    vec3 lightNormal = normalize(cross(e1, e2));

    vec3 toLight = hitPoint - previousPoint;
    float dist2 = dot(toLight, toLight);
    if (!(dist2 > 1e-8)) return 0.0;

    vec3 wi = normalize(toLight);
    float cosLight = max(dot(lightNormal, -wi), 0.0);
    if (cosLight <= 1e-7) return 0.0;

    float pArea = light.stats.z / light.stats.y;
    return pArea * dist2 / max(cosLight, 1e-6);
}

#endif
