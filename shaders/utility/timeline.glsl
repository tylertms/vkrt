#ifndef TIMELINE_GLSL
#define TIMELINE_GLSL

#include "bindings.glsl"

struct TimelineSample {
    float emissionScale;
    vec3 emissionTint;
};

TimelineSample loadTimelineKey(uint keyIndex) {
    TimelineSample s;
    s.emissionScale = scene.timelineTimeScale[keyIndex].y;
    s.emissionTint = scene.timelineTint[keyIndex].xyz;
    return s;
}

bool timelineIsActive() {
    return scene.timelineEnabled != 0u &&
        scene.timelineKeyframeCount > 0u &&
        scene.timeBase >= 0.0 &&
        scene.timeStep >= scene.timeBase;
}

TimelineSample sampleTimeline(float queryTime) {
    uint keyCount = min(scene.timelineKeyframeCount, uint(VKRT_SCENE_TIMELINE_MAX_KEYFRAMES));
    if (keyCount == 1u) {
        return loadTimelineKey(0u);
    }

    float firstTime = scene.timelineTimeScale[0].x;
    if (queryTime < firstTime) {
        return loadTimelineKey(0u);
    }

    uint lastIndex = keyCount - 1u;
    float lastTime = scene.timelineTimeScale[lastIndex].x;
    if (queryTime > lastTime) {
        return loadTimelineKey(lastIndex);
    }

    for (uint i = 0u; i + 1u < keyCount; i++) {
        float tB = scene.timelineTimeScale[i + 1u].x;
        if (queryTime < tB) {
            return loadTimelineKey(i);
        }
    }

    return loadTimelineKey(lastIndex);
}

void applyTimelineEmission(inout Material material, TimelineSample timelineValue) {
    material.emissionStrength *= max(timelineValue.emissionScale, 0.0);
    material.emissionColor *= max(timelineValue.emissionTint, vec3(0.0));
}

#endif
