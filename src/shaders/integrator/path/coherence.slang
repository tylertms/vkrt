#ifndef VKRT_INTEGRATOR_PATH_COHERENCE_SLANG
#define VKRT_INTEGRATOR_PATH_COHERENCE_SLANG

#include "./state.slang"

static const uint VKRT_COHERENCE_HINT_REFRACTIVE = 1u;
static const uint VKRT_COHERENCE_HINT_ABSORPTION = 2u;
static const uint VKRT_COHERENCE_HINT_SPECTRAL = 4u;
static const uint VKRT_COHERENCE_HINT_NEE_ALLOWED = 16u;

uint buildCommonSceneRayCoherenceHint(PathCommonState pathState, uint modeBits) {
    uint hint = modeBits;
    if (pathState.medium.refractiveActive()) hint |= VKRT_COHERENCE_HINT_REFRACTIVE;
    if (pathState.medium.absorptionActive()) hint |= VKRT_COHERENCE_HINT_ABSORPTION;
    if (pathPrevVertexNeeAllowed(pathState)) hint |= VKRT_COHERENCE_HINT_NEE_ALLOWED;
    return hint;
}

uint buildSceneRayCoherenceHint(RgbPathState pathState) {
    return buildCommonSceneRayCoherenceHint(pathState.common, 0u);
}

uint buildSceneRayCoherenceHint(SpectralSinglePathState pathState) {
    return buildCommonSceneRayCoherenceHint(pathState.common, VKRT_COHERENCE_HINT_SPECTRAL);
}

uint buildSceneRayCoherenceHint(SpectralHeroPathState pathState) {
    return buildCommonSceneRayCoherenceHint(pathState.common, VKRT_COHERENCE_HINT_SPECTRAL);
}

#endif
