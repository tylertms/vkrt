#ifndef VKRT_INTEGRATOR_PATH_COHERENCE_SLANG
#define VKRT_INTEGRATOR_PATH_COHERENCE_SLANG

#include "./state.slang"

uint buildCommonSceneRayCoherenceHint(PathCommonState pathState, uint modeBits) {
    uint hint = modeBits;
    if (pathState.medium.refractiveActive()) {
        hint |= 1u;
    }
    if (pathState.medium.absorptionActive()) {
        hint |= 2u;
    }
    if (pathPrevVertexNeeAllowed(pathState)) {
        hint |= 16u;
    }
    return hint;
}

uint buildSceneRayCoherenceHint(RgbPathState pathState) {
    return buildCommonSceneRayCoherenceHint(pathState.common, 0u);
}

uint buildSceneRayCoherenceHint(SpectralSinglePathState pathState) {
    return buildCommonSceneRayCoherenceHint(pathState.common, 4u);
}

uint buildSceneRayCoherenceHint(SpectralHeroPathState pathState) {
    return buildCommonSceneRayCoherenceHint(pathState.common, 4u);
}

#endif
