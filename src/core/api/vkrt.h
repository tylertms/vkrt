#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "cglm.h"
#include "constants.h"

#define WIDTH 1600
#define HEIGHT 900

#define MAX_FRAMES_IN_FLIGHT 2

#define VKRT_RENDER_VIEW_ZOOM_MIN 1.0f
#define VKRT_RENDER_VIEW_ZOOM_MAX 64.0f
typedef struct VKRT_SceneTimelineKeyframe {
    float time;
    float emissionScale;
    vec3 emissionTint;
} VKRT_SceneTimelineKeyframe;

typedef struct VKRT_SceneTimelineSettings {
    uint8_t enabled;
    uint32_t keyframeCount;
    VKRT_SceneTimelineKeyframe keyframes[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
} VKRT_SceneTimelineSettings;

typedef struct SceneData {
    mat4 viewInverse;
    mat4 projInverse;
    uint32_t frameNumber;
    uint32_t samplesPerPixel;
    uint32_t maxBounces;
    uint32_t toneMappingMode;
    uint32_t viewportRect[4];
    float timeBase;
    float timeStep;
    float fogDensity;
    uint32_t debugMode;
    uint32_t timelineEnabled;
    uint32_t timelineKeyframeCount;
    uint32_t emissiveMeshCount;
    uint32_t emissiveTriangleCount;
    uint32_t neeEnabled;
    uint32_t misEnabled;
    uint32_t selectionEnabled;
    uint32_t selectedMeshIndex;
    vec4 timelineTimeScale[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
    vec4 timelineTint[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
} SceneData;

typedef enum VKRT_ToneMappingMode {
    VKRT_TONE_MAPPING_NONE = 0,
    VKRT_TONE_MAPPING_ACES = 1,
} VKRT_ToneMappingMode;

typedef struct Camera {
    vec3 pos, target, up;
    uint32_t width, height;
    float nearZ, farZ, vfov;
} Camera;

typedef struct VKRT_CameraInput {
    float orbitDx;
    float orbitDy;
    float panDx;
    float panDy;
    float scroll;
    uint8_t orbiting;
    uint8_t panning;
    uint8_t captureMouse;
} VKRT_CameraInput;

typedef struct VKRT_ShaderConfig {
    const char* rgenPath;
    const char* rmissPath;
    const char* rchitPath;
} VKRT_ShaderConfig;

typedef struct VKRT_CreateInfo {
    uint32_t width;
    uint32_t height;
    const char* title;
    uint8_t vsync;
    VKRT_ShaderConfig shaders;
} VKRT_CreateInfo;

typedef struct Vertex {
    vec4 position;
    vec4 normal;
} Vertex;

typedef struct AccelerationStructure {
    VkAccelerationStructureKHR structure;
    VkDeviceMemory memory;
    VkBuffer buffer;
    VkDeviceAddress deviceAddress;
    uint8_t needsRebuild;
} AccelerationStructure;

typedef struct MeshInfo {
    vec3 position;
    uint32_t vertexBase;
    vec3 rotation;
    uint32_t vertexCount;
    vec3 scale;
    uint32_t indexBase;
    uint32_t indexCount;
    uint32_t materialIndex;
    uint32_t renderBackfaces;
    uint32_t padding;
} MeshInfo;

typedef struct MaterialData {
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
    uint32_t geometryThinWalled;
    vec3 geometryNormal;
    float paddingGeometryNormal;
    vec3 geometryTangent;
    float paddingGeometryTangent;
    vec3 geometryCoatNormal;
    float paddingGeometryCoatNormal;
    vec3 geometryCoatTangent;
    float paddingGeometryCoatTangent;
} MaterialData;

static inline MaterialData VKRT_materialDataOpenPBRDefault(void) {
    return (MaterialData){
        .baseWeight = 1.0f,
        .baseColor = {0.8f, 0.8f, 0.8f},
        .baseMetalness = 0.0f,
        .baseDiffuseRoughness = 0.0f,
        .specularWeight = 1.0f,
        .specularColor = {1.0f, 1.0f, 1.0f},
        .specularRoughness = 0.3f,
        .specularRoughnessAnisotropy = 0.0f,
        .specularIor = 1.5f,
        .transmissionWeight = 0.0f,
        .transmissionColor = {1.0f, 1.0f, 1.0f},
        .transmissionDepth = 0.0f,
        .transmissionScatter = {0.0f, 0.0f, 0.0f},
        .transmissionScatterAnisotropy = 0.0f,
        .transmissionDispersionScale = 0.0f,
        .transmissionDispersionAbbeNumber = 20.0f,
        .subsurfaceWeight = 0.0f,
        .subsurfaceColor = {0.8f, 0.8f, 0.8f},
        .subsurfaceRadius = 1.0f,
        .subsurfaceRadiusScale = {1.0f, 0.5f, 0.25f},
        .subsurfaceScatterAnisotropy = 0.0f,
        .coatWeight = 0.0f,
        .coatColor = {1.0f, 1.0f, 1.0f},
        .coatRoughness = 0.0f,
        .coatRoughnessAnisotropy = 0.0f,
        .coatIor = 1.6f,
        .coatDarkening = 1.0f,
        .fuzzWeight = 0.0f,
        .fuzzColor = {1.0f, 1.0f, 1.0f},
        .fuzzRoughness = 0.5f,
        .emissionLuminance = 0.0f,
        .emissionColor = {1.0f, 1.0f, 1.0f},
        .thinFilmWeight = 0.0f,
        .thinFilmThickness = 0.5f,
        .thinFilmIor = 1.4f,
        .geometryOpacity = 1.0f,
        .geometryThinWalled = 0u,
        .geometryNormal = {0.0f, 0.0f, 1.0f},
        .geometryTangent = {1.0f, 0.0f, 0.0f},
        .geometryCoatNormal = {0.0f, 0.0f, 1.0f},
        .geometryCoatTangent = {1.0f, 0.0f, 0.0f},
    };
}

typedef struct Mesh {
    MeshInfo info;
    MaterialData material;
    AccelerationStructure bottomLevelAccelerationStructure;
    Vertex* vertices;
    uint32_t* indices;
    uint32_t geometrySource;
    uint8_t ownsGeometry;
} Mesh;

typedef struct PickBuffer {
    uint32_t pixel; // (x | (y << 16))
    uint32_t requestID;
    uint32_t hitMeshIndex;
    uint32_t resultID;
} PickBuffer;

typedef struct Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceAddress deviceAddress;
    uint32_t count;
} Buffer;

struct VKRT;

typedef struct QueueFamily {
    int32_t graphics;
    int32_t present;
} QueueFamily;

typedef struct VKRT_AppHooks {
    void (*init)(struct VKRT* vkrt, void* userData);
    void (*deinit)(struct VKRT* vkrt, void* userData);
    void (*drawOverlay)(struct VKRT* vkrt, VkCommandBuffer commandBuffer, void* userData);
    void* userData;
} VKRT_AppHooks;

typedef struct VKRT_DeviceProcedures {
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
} VKRT_DeviceProcedures;

typedef struct VKRT_Core {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    QueueFamily indices;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    VkPipelineLayout pipelineLayout;
    VkPipeline rayTracingPipeline;
    VkBuffer shaderBindingTableBuffer;
    VkDeviceMemory shaderBindingTableMemory;
    VkStridedDeviceAddressRegionKHR shaderBindingTables[4];
    VkBuffer sceneDataBuffer;
    VkDeviceMemory sceneDataMemory;
    SceneData* sceneData;
    PickBuffer* pickData;
    VkImage storageImage;
    VkImageView storageImageView;
    VkDeviceMemory storageImageMemory;
    VkImage accumulationImages[2];
    VkImageView accumulationImageViews[2];
    VkDeviceMemory accumulationImageMemories[2];
    uint32_t accumulationReadIndex;
    uint32_t accumulationWriteIndex;
    VkBool32 accumulationNeedsReset;
    Mesh* meshes;
    AccelerationStructure topLevelAccelerationStructure;
    Buffer pickBuffer;
    Buffer vertexData;
    Buffer indexData;
    Buffer meshData;
    Buffer materialData;
    Buffer emissiveMeshData;
    Buffer emissiveTriangleData;
    VkBool32 materialDataDirty;
    VkBool32 descriptorSetReady;
    uint32_t emissiveMeshCount;
    uint32_t emissiveTriangleCount;
    char deviceName[256];
    uint32_t vendorID;
    uint32_t driverVersion;
    VKRT_ShaderConfig shaders;
    VKRT_DeviceProcedures procs;
} VKRT_Core;

typedef struct VKRT_Runtime {
    GLFWwindow* window;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapChain;
    VkImage* swapChainImages;
    VkImageView* swapChainImageViews;
    size_t swapChainImageCount;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    VkExtent2D renderExtent;
    uint32_t displayViewportRect[4];
    VkRenderPass renderPass;
    VkFramebuffer* framebuffers;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore* renderFinishedSemaphores;
    VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];
    uint32_t currentFrame;
    VkBool32 framebufferResized;
    VkQueryPool timestampPool;
    float timestampPeriod;
    uint8_t vsync;
    uint8_t savedVsync;
    uint32_t frameImageIndex;
    VkBool32 frameAcquired;
    VkBool32 frameSubmitted;
    VkBool32 framePresented;
    VkBool32 frameTraced;
    VkPresentModeKHR presentMode;
    float displayRefreshHz;
    uint32_t autoSPPFastAdaptFrames;
    VkBool32 swapchainFormatLogInitialized;
    VkFormat lastLoggedSwapchainFormat;
    VkColorSpaceKHR lastLoggedSwapchainColorSpace;
} VKRT_Runtime;

typedef struct VKRT_PublicState {
    Camera camera;
    uint32_t framesPerSecond;
    uint64_t lastFrameTimestamp;
    uint8_t frametimeStartIndex;
    float averageFrametime;
    float frametimes[128];
    float displayTimeMs;
    float renderTimeMs;
    uint32_t accumulationFrame;
    uint32_t samplesPerPixel;
    uint64_t totalSamples;
    uint8_t renderModeActive;
    uint8_t renderModeFinished;
    uint32_t renderTargetSamples;
    float renderViewZoom;
    float renderViewPanX;
    float renderViewPanY;
    float displayRenderTimeMs;
    float displayFrameTimeMs;
    uint32_t maxBounces;
    VKRT_ToneMappingMode toneMappingMode;
    uint8_t autoSPPEnabled;
    uint32_t autoSPPTargetFPS;
    float autoSPPTargetFrameMs;
    float autoSPPControlMs;
    uint32_t autoSPPFramesUntilNextAdjust;
    float fogDensity;
    float timeBase;
    float timeStep;
    uint32_t debugMode;
    uint8_t neeEnabled;
    uint8_t misEnabled;
    uint32_t selectionEnabled;
    uint32_t selectedMeshIndex;
    VKRT_SceneTimelineSettings sceneTimeline;
} VKRT_PublicState;

typedef struct VKRT {
    VKRT_Core core;
    VKRT_Runtime runtime;
    VKRT_PublicState state;
    VKRT_AppHooks appHooks;
} VKRT;

void VKRT_defaultCreateInfo(VKRT_CreateInfo* createInfo);
int VKRT_initWithCreateInfo(VKRT* vkrt, const VKRT_CreateInfo* createInfo);
int VKRT_init(VKRT* vkrt);
void VKRT_registerAppHooks(VKRT* vkrt, VKRT_AppHooks hooks);
void VKRT_deinit(VKRT* vkrt);
int VKRT_shouldDeinit(VKRT* vkrt);
void VKRT_poll(VKRT* vkrt);

void VKRT_beginFrame(VKRT* vkrt);
void VKRT_updateScene(VKRT* vkrt);
void VKRT_trace(VKRT* vkrt);
void VKRT_present(VKRT* vkrt);
void VKRT_endFrame(VKRT* vkrt);

void VKRT_draw(VKRT* vkrt);

void VKRT_uploadMeshData(VKRT* vkrt, const Vertex* vertices, size_t vertexCount, const uint32_t* indices, size_t indexCount);
int VKRT_removeMesh(VKRT* vkrt, uint32_t meshIndex);
void VKRT_updateTLAS(VKRT* vkrt);
void VKRT_applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input);
void VKRT_invalidateAccumulation(VKRT* vkrt);
void VKRT_setSamplesPerPixel(VKRT* vkrt, uint32_t samplesPerPixel);
void VKRT_setAutoSPPEnabled(VKRT* vkrt, uint8_t enabled);
void VKRT_setAutoSPPTargetFPS(VKRT* vkrt, uint32_t targetFPS);
void VKRT_setToneMappingMode(VKRT* vkrt, VKRT_ToneMappingMode toneMappingMode);
void VKRT_setFogDensity(VKRT* vkrt, float fogDensity);
void VKRT_setDebugMode(VKRT* vkrt, uint32_t mode);
void VKRT_setNEEEnabled(VKRT* vkrt, uint8_t enabled);
void VKRT_setMISEnabled(VKRT* vkrt, uint8_t enabled);
void VKRT_setTimeRange(VKRT* vkrt, float timeBase, float timeStep);
void VKRT_setSceneTimeline(VKRT* vkrt, const VKRT_SceneTimelineSettings* timeline);
int VKRT_saveRenderPNG(VKRT* vkrt, const char* path);
int VKRT_startRender(VKRT* vkrt, uint32_t width, uint32_t height, uint32_t targetSamples);
void VKRT_stopRenderSampling(VKRT* vkrt);
void VKRT_stopRender(VKRT* vkrt);

uint32_t VKRT_getMeshCount(const VKRT* vkrt);
int VKRT_setMeshTransform(VKRT* vkrt, uint32_t meshIndex, vec3 position, vec3 rotation, vec3 scale);
int VKRT_setMeshMaterial(VKRT* vkrt, uint32_t meshIndex, const MaterialData* material);
int VKRT_setMeshRenderBackfaces(VKRT* vkrt, uint32_t meshIndex, uint8_t renderBackfaces);
void VKRT_pickMeshAtPixel(const VKRT* vkrt, uint32_t x, uint32_t y, uint32_t* outMeshIndex);
void VKRT_setSelectedMesh(VKRT* vkrt, uint32_t meshIndex);
void VKRT_getSelectedMesh(const VKRT* vkrt, uint32_t* outMeshIndex);
void VKRT_setRenderViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
void VKRT_cameraSetPose(VKRT* vkrt, vec3 position, vec3 target, vec3 up, float vfov);
void VKRT_cameraGetPose(const VKRT* vkrt, vec3 position, vec3 target, vec3 up, float* vfov);
void VKRT_framebufferResizedCallback(GLFWwindow* window, int width, int height);

#define COUNT_OF(x) ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))

#ifdef __cplusplus
}
#endif
