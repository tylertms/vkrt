#ifndef BSDF_SPECULAR
#define BSDF_SPECULAR

vec3 specularBSDF(vec3 normal, vec3 incident, inout uint state) {
    return normalize(reflect(incident, normal));
}

#endif
