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
VKRT_Result VKRT_uploadMeshDataBatch(VKRT* vkrt, const VKRT_MeshUpload* uploads, size_t uploadCount);
VKRT_Result VKRT_removeMesh(VKRT* vkrt, uint32_t meshIndex);
VKRT_Result VKRT_applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input);
VKRT_Result VKRT_invalidateAccumulation(VKRT* vkrt);
VKRT_Result VKRT_setSamplesPerPixel(VKRT* vkrt, uint32_t samplesPerPixel);
VKRT_Result VKRT_setPathDepth(VKRT* vkrt, uint32_t rrMinDepth, uint32_t rrMaxDepth);
VKRT_Result VKRT_setAutoSPPEnabled(VKRT* vkrt, uint8_t enabled);
VKRT_Result VKRT_setAutoSPPTargetFPS(VKRT* vkrt, uint32_t targetFPS);
VKRT_Result VKRT_setToneMappingMode(VKRT* vkrt, VKRT_ToneMappingMode toneMappingMode);
VKRT_Result VKRT_setExposure(VKRT* vkrt, float exposure);
VKRT_Result VKRT_setAutoExposureEnabled(VKRT* vkrt, uint8_t enabled);
VKRT_Result VKRT_setEnvironmentLight(VKRT* vkrt, vec3 color, float strength);
VKRT_Result VKRT_setEnvironmentTextureFromFile(VKRT* vkrt, const char* path);
VKRT_Result VKRT_clearEnvironmentTexture(VKRT* vkrt);
VKRT_Result VKRT_setDebugMode(VKRT* vkrt, uint32_t mode);
VKRT_Result VKRT_setMISNEEEnabled(VKRT* vkrt, uint32_t enabled);
VKRT_Result VKRT_setTimeRange(VKRT* vkrt, float timeBase, float timeStep);
VKRT_Result VKRT_setSceneTimeline(VKRT* vkrt, const VKRT_SceneTimelineSettings* timeline);
void VKRT_defaultRenderExportSettings(VKRT_RenderExportSettings* settings);
VKRT_Result VKRT_setRenderDenoiseEnabled(VKRT* vkrt, uint8_t enabled);
VKRT_Result VKRT_denoiseRenderToViewport(VKRT* vkrt);
VKRT_Result VKRT_saveRenderImageEx(VKRT* vkrt, const char* path, const VKRT_RenderExportSettings* settings);
VKRT_Result VKRT_saveRenderImage(VKRT* vkrt, const char* path);
VKRT_Result VKRT_saveRenderPNG(VKRT* vkrt, const char* path);
VKRT_Result VKRT_startRender(VKRT* vkrt, uint32_t width, uint32_t height, uint32_t targetSamples);
VKRT_Result VKRT_stopRenderSampling(VKRT* vkrt);
VKRT_Result VKRT_stopRender(VKRT* vkrt);
VKRT_Result VKRT_getSceneSettings(const VKRT* vkrt, VKRT_SceneSettingsSnapshot* outSettings);
VKRT_Result VKRT_getRenderStatus(const VKRT* vkrt, VKRT_RenderStatusSnapshot* outStatus);
VKRT_Result VKRT_getRuntimeSnapshot(const VKRT* vkrt, VKRT_RuntimeSnapshot* outRuntime);
VKRT_Result VKRT_getSystemInfo(const VKRT* vkrt, VKRT_SystemInfo* outSystemInfo);
VKRT_Result VKRT_getRenderSourceExtent(const VKRT* vkrt, float* outWidth, float* outHeight);
VKRT_Result VKRT_getDisplayViewportExtent(const VKRT* vkrt, float* outWidth, float* outHeight);
VKRT_Result VKRT_getRenderViewCrop(const VKRT* vkrt, float zoom, float* outWidth, float* outHeight);
VKRT_Result VKRT_getRenderViewState(const VKRT* vkrt, float* outZoom, float* outPanX, float* outPanY);
VKRT_Result VKRT_setRenderViewState(VKRT* vkrt, float zoom, float panX, float panY);

VKRT_Result VKRT_getMeshCount(const VKRT* vkrt, uint32_t* outMeshCount);
VKRT_Result VKRT_getMeshSnapshot(const VKRT* vkrt, uint32_t meshIndex, VKRT_MeshSnapshot* outMesh);
VKRT_Result VKRT_getMaterialCount(const VKRT* vkrt, uint32_t* outMaterialCount);
VKRT_Result VKRT_getMaterialSnapshot(const VKRT* vkrt, uint32_t materialIndex, VKRT_MaterialSnapshot* outMaterial);
VKRT_Result VKRT_getTextureCount(const VKRT* vkrt, uint32_t* outTextureCount);
VKRT_Result VKRT_getTextureSnapshot(const VKRT* vkrt, uint32_t textureIndex, VKRT_TextureSnapshot* outTexture);
VKRT_Result VKRT_addTextureFromPixels(
    VKRT* vkrt,
    const VKRT_TextureUpload* upload,
    uint32_t* outTextureIndex
);
VKRT_Result VKRT_addTextureFromFile(
    VKRT* vkrt,
    const char* path,
    const char* name,
    uint32_t colorSpace,
    uint32_t* outTextureIndex
);
VKRT_Result VKRT_removeTexture(VKRT* vkrt, uint32_t textureIndex);
VKRT_Result VKRT_addTexturesBatch(
    VKRT* vkrt,
    const VKRT_TextureUpload* uploads,
    size_t uploadCount,
    uint32_t* outTextureIndices
);
VKRT_Result VKRT_setMaterialTexture(
    VKRT* vkrt,
    uint32_t materialIndex,
    uint32_t textureSlot,
    uint32_t textureIndex
);
VKRT_Result VKRT_addMaterial(VKRT* vkrt, const Material* material, const char* name, uint32_t* outMaterialIndex);
VKRT_Result VKRT_removeMaterial(VKRT* vkrt, uint32_t materialIndex);
VKRT_Result VKRT_setMaterialName(VKRT* vkrt, uint32_t materialIndex, const char* name);
VKRT_Result VKRT_setMaterial(VKRT* vkrt, uint32_t materialIndex, const Material* material);
VKRT_Result VKRT_setMeshMaterialIndex(VKRT* vkrt, uint32_t meshIndex, uint32_t materialIndex);
VKRT_Result VKRT_clearMeshMaterialAssignment(VKRT* vkrt, uint32_t meshIndex);
VKRT_Result VKRT_setMeshOpacity(VKRT* vkrt, uint32_t meshIndex, float opacity);
VKRT_Result VKRT_setMeshName(VKRT* vkrt, uint32_t meshIndex, const char* name);
VKRT_Result VKRT_setMeshTransform(VKRT* vkrt, uint32_t meshIndex, vec3 position, vec3 rotation, vec3 scale);
VKRT_Result VKRT_setMeshTransformMatrix(VKRT* vkrt, uint32_t meshIndex, mat4 worldTransform);
VKRT_Result VKRT_setMeshRenderBackfaces(VKRT* vkrt, uint32_t meshIndex, uint32_t enabled);
VKRT_Result VKRT_requestSelectionAtPixel(VKRT* vkrt, uint32_t x, uint32_t y);
VKRT_Result VKRT_consumeSelectedMesh(VKRT* vkrt, uint32_t* outMeshIndex, uint8_t* outReady);
VKRT_Result VKRT_setSelectedMesh(VKRT* vkrt, uint32_t meshIndex);
VKRT_Result VKRT_getSelectedMesh(const VKRT* vkrt, uint32_t* outMeshIndex);
VKRT_Result VKRT_setRenderViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
VKRT_Result VKRT_cameraSetPose(VKRT* vkrt, vec3 position, vec3 target, vec3 upVector, float vfov);
VKRT_Result VKRT_cameraGetPose(const VKRT* vkrt, vec3 position, vec3 target, vec3 upVector, float* vfov);

void buildMeshTransformMatrix(const vec3 position, const vec3 rotationDegrees, const vec3 scale, mat4 outMatrix);
void buildImportedMeshNodeTransformMatrix(mat4 worldTransform, mat4 outEngineTransform);
void decomposeImportedMeshTransform(mat4 worldTransform, vec3 outPosition, vec3 outRotation, vec3 outScale);
void decomposeImportedMeshNodeTransform(mat4 worldTransform, vec3 outPosition, vec3 outRotation, vec3 outScale);

#ifdef __cplusplus
}
#endif
