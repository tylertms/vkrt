#pragma once

#include "vkrt.h"
#include "vkrt_engine_types.h"

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

typedef enum DeviceExtensionBits {
    DEVICE_EXTENSION_SWAPCHAIN_BIT = 1u << 0,
    DEVICE_EXTENSION_ACCELERATION_STRUCTURE_BIT = 1u << 1,
    DEVICE_EXTENSION_RAY_TRACING_PIPELINE_BIT = 1u << 2,
    DEVICE_EXTENSION_DEFERRED_HOST_OPERATIONS_BIT = 1u << 3,
    DEVICE_EXTENSION_BUFFER_DEVICE_ADDRESS_BIT = 1u << 4,
    DEVICE_EXTENSION_RAY_TRACING_INVOCATION_REORDER_BIT = 1u << 5
} DeviceExtensionBits;

typedef struct DeviceExtensionSupport {
    uint32_t requiredMask;
    uint32_t optionalMask;
    uint32_t availableMask;
    uint32_t enabledMask;
    uint32_t missingRequiredMask;
} DeviceExtensionSupport;

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
    VkDescriptorSet descriptorSets[VKRT_MAX_FRAMES_IN_FLIGHT];
    VkPipelineLayout pipelineLayout;
    VkPipeline rayTracingPipeline;
    VkPipeline selectionRayTracingPipeline;
    VkPipeline computePipeline;
    VkBuffer shaderBindingTableBuffer;
    VkDeviceMemory shaderBindingTableMemory;
    VkStridedDeviceAddressRegionKHR shaderBindingTables[4];
    VkBuffer selectionShaderBindingTableBuffer;
    VkDeviceMemory selectionShaderBindingTableMemory;
    VkStridedDeviceAddressRegionKHR selectionShaderBindingTables[4];
    SceneData sceneDataHost;
    SceneData* sceneData;
    VkBuffer sceneDataBuffers[VKRT_MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory sceneDataMemories[VKRT_MAX_FRAMES_IN_FLIGHT];
    SceneData* sceneFrameData[VKRT_MAX_FRAMES_IN_FLIGHT];
    PickBuffer* pickData;
    uint32_t pickPendingFrame;
    uint32_t pickResultMeshIndex;
    uint8_t pickPending;
    uint8_t pickSubmitted;
    uint8_t pickResultReady;
    VkImage outputImage;
    VkImageView outputImageView;
    VkDeviceMemory outputImageMemory;
    VkImage accumulationImages[2];
    VkImageView accumulationImageViews[2];
    VkDeviceMemory accumulationImageMemories[2];
    uint32_t accumulationReadIndex;
    uint32_t accumulationWriteIndex;
    VkBool32 accumulationNeedsReset;
    VkBool32 selectionMaskDirty;
    VkImage selectionMaskImage;
    VkImageView selectionMaskImageView;
    VkDeviceMemory selectionMaskImageMemory;
    Mesh* meshes;
    Buffer pickBuffer;
    Buffer vertexData;
    Buffer indexData;
    uint32_t meshCount;
    Buffer sceneMeshData;
    Buffer sceneMaterialData;
    Buffer sceneEmissiveMeshData;
    Buffer sceneEmissiveTriangleData;
    AccelerationStructure sceneTopLevelAccelerationStructure;
    VkBool32 descriptorSetReady[VKRT_MAX_FRAMES_IN_FLIGHT];
    uint32_t sceneRevision;
    uint32_t materialRevision;
    uint32_t lightRevision;
    uint32_t sceneResourceRevision;
    uint32_t materialResourceRevision;
    uint32_t lightResourceRevision;
    GeometryLayout geometryLayout;
    uint32_t emissiveMeshCount;
    uint32_t emissiveTriangleCount;
    DeviceExtensionSupport deviceExtensionSupport;
    char deviceName[VKRT_DEVICE_NAME_LEN];
    uint32_t vendorID;
    uint32_t driverVersion;
    VKRT_DeviceProcedures procs;
} VKRT_Core;

typedef struct FrameTransfer {
    VkBuffer buffer;
    VkDeviceMemory memory;
} FrameTransfer;

typedef struct PendingGeometryUpload {
    uint32_t meshIndex;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkDeviceSize indexOffset;
} PendingGeometryUpload;

typedef struct PendingBufferCopy {
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkBuffer dstBuffer;
    VkDeviceSize size;
} PendingBufferCopy;

typedef struct PendingBLASBuild {
    uint32_t meshIndex;
    VkBuffer scratchBuffer;
    VkDeviceMemory scratchMemory;
} PendingBLASBuild;

typedef struct FrameSceneUpdate {
    PendingBufferCopy* sceneTransfers;
    uint32_t sceneTransferCount;
    PendingGeometryUpload* geometryUploads;
    uint32_t geometryUploadCount;
    PendingBLASBuild* blasBuilds;
    uint32_t blasBuildCount;
    FrameTransfer instanceBuffer;
    FrameTransfer tlasScratch;
    VkBool32 tlasBuildPending;
} FrameSceneUpdate;

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
    uint32_t displayWidth;
    uint32_t displayHeight;
    uint32_t displayViewportRect[4];
    VkRenderPass renderPass;
    VkFramebuffer* framebuffers;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffers[VKRT_MAX_FRAMES_IN_FLIGHT];
    FrameSceneUpdate frameSceneUpdates[VKRT_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore imageAvailableSemaphores[VKRT_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore* renderFinishedSemaphores;
    VkFence inFlightFences[VKRT_MAX_FRAMES_IN_FLIGHT];
    uint32_t currentFrame;
    VkBool32 framebufferResized;
    VkQueryPool timestampPool;
    float timestampPeriod;
    VkBool32 frameTimingPending[VKRT_MAX_FRAMES_IN_FLIGHT];
    uint32_t frameImageIndex;
    VkBool32 frameAcquired;
    VkBool32 frameOffscreen;
    VkBool32 frameSubmitted;
    VkBool32 framePresented;
    VkBool32 frameTraced;
    VkBool32 frameSelectionTraced;
    VkBool32 headless;
    VkPresentModeKHR presentMode;
    float displayRefreshHz;
    VkBool32 swapChainFormatLogInitialized;
    VkFormat lastLoggedSwapChainFormat;
    VkColorSpaceKHR lastLoggedSwapChainColorSpace;
    uint8_t appInitialized;
} VKRT_Runtime;

typedef struct VKRT_RenderViewState {
    float zoom;
    float panX;
    float panY;
} VKRT_RenderViewState;

typedef struct VKRT_TimingState {
    uint64_t lastFrameTimestamp;
    uint8_t frametimeStartIndex;
} VKRT_TimingState;

typedef struct VKRT_AutoSPPState {
    float targetFrameMs;
    float controlMs;
} VKRT_AutoSPPState;

typedef struct VKRT {
    VKRT_Core core;
    VKRT_Runtime runtime;
    VKRT_SceneSettingsSnapshot sceneSettings;
    VKRT_RenderStatusSnapshot renderStatus;
    VKRT_RenderViewState renderView;
    VKRT_TimingState timing;
    VKRT_AutoSPPState autoSPP;
    VKRT_AppHooks appHooks;
} VKRT;

static inline FrameSceneUpdate* vkrtCurrentFrameSceneUpdate(VKRT* vkrt) {
    return &vkrt->runtime.frameSceneUpdates[vkrt->runtime.currentFrame];
}
