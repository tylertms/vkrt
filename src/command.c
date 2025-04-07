#include "command.h"
#include "device.h"

#include <stdlib.h>

void createCommandPool(VKRT* vkrt) {
    QueueFamily indices = findQueueFamilies(vkrt);

    VkCommandPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = indices.graphics;

    if (vkCreateCommandPool(vkrt->device, &poolInfo, NULL, &vkrt->commandPool) != VK_SUCCESS) {
        perror("ERROR: Failed to create command pool");
        exit(EXIT_FAILURE);
    }
}