#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "vkrt_types.h"

void VKRT_defaultCreateInfo(VKRT_CreateInfo* createInfo);
VKRT_Result VKRT_create(VKRT** outVkrt);
void VKRT_destroy(VKRT* vkrt);
VKRT_Result VKRT_initWithCreateInfo(VKRT* vkrt, const VKRT_CreateInfo* createInfo);
VKRT_Result VKRT_init(VKRT* vkrt);
void VKRT_registerAppHooks(VKRT* vkrt, VKRT_AppHooks hooks);
void VKRT_deinit(VKRT* vkrt);
int VKRT_shouldDeinit(VKRT* vkrt);
void VKRT_poll(VKRT* vkrt);

VKRT_Result VKRT_beginFrame(VKRT* vkrt);
VKRT_Result VKRT_updateScene(VKRT* vkrt);
VKRT_Result VKRT_trace(VKRT* vkrt);
VKRT_Result VKRT_present(VKRT* vkrt);
VKRT_Result VKRT_endFrame(VKRT* vkrt);

VKRT_Result VKRT_draw(VKRT* vkrt);

VKRT_Result VKRT_uploadMeshData(VKRT* vkrt, const Vertex* vertices, size_t vertexCount, const uint32_t* indices, size_t indexCount);
VKRT_Result VKRT_removeMesh(VKRT* vkrt, uint32_t meshIndex);
VKRT_Result VKRT_updateTLAS(VKRT* vkrt);
VKRT_Result VKRT_applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input);
VKRT_Result VKRT_invalidateAccumulation(VKRT* vkrt);
VKRT_Result VKRT_setSamplesPerPixel(VKRT* vkrt, uint32_t samplesPerPixel);
VKRT_Result VKRT_setPathDepth(VKRT* vkrt, uint32_t rrMinDepth, uint32_t rrMaxDepth);
VKRT_Result VKRT_setAutoSPPEnabled(VKRT* vkrt, uint8_t enabled);
VKRT_Result VKRT_setAutoSPPTargetFPS(VKRT* vkrt, uint32_t targetFPS);
VKRT_Result VKRT_setToneMappingMode(VKRT* vkrt, VKRT_ToneMappingMode toneMappingMode);
VKRT_Result VKRT_setFogDensity(VKRT* vkrt, float fogDensity);
VKRT_Result VKRT_setDebugMode(VKRT* vkrt, uint32_t mode);
VKRT_Result VKRT_setMISNEEEnabled(VKRT* vkrt, uint32_t enabled);
VKRT_Result VKRT_setTimeRange(VKRT* vkrt, float timeBase, float timeStep);
VKRT_Result VKRT_setSceneTimeline(VKRT* vkrt, const VKRT_SceneTimelineSettings* timeline);
VKRT_Result VKRT_saveRenderPNG(VKRT* vkrt, const char* path);
VKRT_Result VKRT_startRender(VKRT* vkrt, uint32_t width, uint32_t height, uint32_t targetSamples);
VKRT_Result VKRT_stopRenderSampling(VKRT* vkrt);
VKRT_Result VKRT_stopRender(VKRT* vkrt);
VKRT_Result VKRT_getPublicState(const VKRT* vkrt, VKRT_PublicState* outState);
VKRT_Result VKRT_getRuntimeSnapshot(const VKRT* vkrt, VKRT_RuntimeSnapshot* outRuntime);
VKRT_Result VKRT_getSystemInfo(const VKRT* vkrt, VKRT_SystemInfo* outSystemInfo);
VKRT_Result VKRT_getOverlayInfo(const VKRT* vkrt, VKRT_OverlayInfo* outOverlayInfo);
VKRT_Result VKRT_setVSyncEnabled(VKRT* vkrt, uint8_t enabled);
VKRT_Result VKRT_getRenderSourceExtent(const VKRT* vkrt, float* outWidth, float* outHeight);
VKRT_Result VKRT_getDisplayViewportExtent(const VKRT* vkrt, float* outWidth, float* outHeight);
VKRT_Result VKRT_getRenderViewCrop(const VKRT* vkrt, float zoom, float* outWidth, float* outHeight);
VKRT_Result VKRT_getRenderViewState(const VKRT* vkrt, float* outZoom, float* outPanX, float* outPanY);
VKRT_Result VKRT_setRenderViewState(VKRT* vkrt, float zoom, float panX, float panY);

VKRT_Result VKRT_getMeshCount(const VKRT* vkrt, uint32_t* outMeshCount);
VKRT_Result VKRT_getMeshSnapshot(const VKRT* vkrt, uint32_t meshIndex, VKRT_MeshSnapshot* outMesh);
VKRT_Result VKRT_setMeshTransform(VKRT* vkrt, uint32_t meshIndex, vec3 position, vec3 rotation, vec3 scale);
VKRT_Result VKRT_setMeshMaterial(VKRT* vkrt, uint32_t meshIndex, const MaterialData* material);
VKRT_Result VKRT_pickMeshAtPixel(const VKRT* vkrt, uint32_t x, uint32_t y, uint32_t* outMeshIndex);
VKRT_Result VKRT_setSelectedMesh(VKRT* vkrt, uint32_t meshIndex);
VKRT_Result VKRT_getSelectedMesh(const VKRT* vkrt, uint32_t* outMeshIndex);
VKRT_Result VKRT_setRenderViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
VKRT_Result VKRT_cameraSetPose(VKRT* vkrt, vec3 position, vec3 target, vec3 up, float vfov);
VKRT_Result VKRT_cameraGetPose(const VKRT* vkrt, vec3 position, vec3 target, vec3 up, float* vfov);
void VKRT_framebufferResizedCallback(GLFWwindow* window, int width, int height);

#ifdef __cplusplus
}
#endif
