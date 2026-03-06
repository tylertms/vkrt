#pragma once

#include "vkrt_internal.h"

VKRT_Result createDescriptorSetLayout(VKRT* vkrt);
VKRT_Result createDescriptorPool(VKRT* vkrt);
VKRT_Result createDescriptorSet(VKRT* vkrt);
VKRT_Result updateDescriptorSet(VKRT* vkrt);
VKRT_Result updateAllDescriptorSets(VKRT* vkrt);
VkBool32 descriptorResourcesReady(VKRT* vkrt);
