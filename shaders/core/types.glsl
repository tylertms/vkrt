#ifndef SHADER_TYPES_GLSL
#define SHADER_TYPES_GLSL

struct Ray {
    vec3 origin;
    vec3 dir;
};

struct Material {
    float baseWeight;
    float paddingBaseWeight[3];
    vec3 baseColor;
    float baseMetalness;
    float baseDiffuseRoughness;
    float specularWeight;
    float paddingSpecularWeight[2];
    vec3 specularColor;
    float specularRoughness;
    float specularRoughnessAnisotropy;
    float specularIor;
    float transmissionWeight;
    float paddingTransmissionWeight;
    vec3 transmissionColor;
    float transmissionDepth;
    vec3 transmissionScatter;
    float transmissionScatterAnisotropy;
    float transmissionDispersionScale;
    float transmissionDispersionAbbeNumber;
    float subsurfaceWeight;
    float paddingSubsurfaceWeight;
    vec3 subsurfaceColor;
    float subsurfaceRadius;
    vec3 subsurfaceRadiusScale;
    float subsurfaceScatterAnisotropy;
    float coatWeight;
    float paddingCoatWeight[3];
    vec3 coatColor;
    float coatRoughness;
    float coatRoughnessAnisotropy;
    float coatIor;
    float coatDarkening;
    float fuzzWeight;
    vec3 fuzzColor;
    float fuzzRoughness;
    float emissionLuminance;
    float paddingEmissionLuminance[3];
    vec3 emissionColor;
    float thinFilmWeight;
    float thinFilmThickness;
    float thinFilmIor;
    float geometryOpacity;
    uint geometryThinWalled;
    vec3 geometryNormal;
    float paddingGeometryNormal;
    vec3 geometryTangent;
    float paddingGeometryTangent;
    vec3 geometryCoatNormal;
    float paddingGeometryCoatNormal;
    vec3 geometryCoatTangent;
    float paddingGeometryCoatTangent;
};

struct Payload {
    vec3 point;
    bool didHit;
    vec3 normal;
    uint materialIndex;
    uint instanceIndex;
    uint primitiveIndex;
    float time;
    float hitDistance;
};

#endif
