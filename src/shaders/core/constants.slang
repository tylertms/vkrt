#ifndef VKRT_CORE_CONSTANTS_SLANG
#define VKRT_CORE_CONSTANTS_SLANG

static const uint VKRT_INVALID_INDEX = 0xffffffffu;
static const uint VKRT_TONE_MAPPING_MODE_NONE = 0u;
static const uint VKRT_TONE_MAPPING_MODE_ACES = 1u;
static const uint VKRT_SCENE_TIMELINE_MAX_KEYFRAMES = 16u;

static const float VKRT_RAY_T_MIN = 0.001;
static const float VKRT_RAY_T_MAX = 10000.0;
static const float VKRT_SHADOW_ORIGIN_OFFSET = 0.001;
static const float VKRT_SHADOW_DISTANCE_OFFSET = 0.002;

static const float VKRT_RR_MIN_CONTINUE_PROB = 0.05;
static const float VKRT_RR_MAX_CONTINUE_PROB = 0.95;

static const float VKRT_SAFE_NORMALIZE_EPS = 1e-12;
static const float VKRT_TANGENT_PARALLEL_THRESHOLD = 0.999;

static const float3 VKRT_OUTLINE_COLOR = float3(1.0, 0.55, 0.1);
static const float VKRT_OUTLINE_REFERENCE_HEIGHT = 1080.0;

#endif
