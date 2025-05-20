# vkrt
This is a minimal, functional implementation of the Vulkan ray tracing pipeline, written in C. It features an interactive, resizable window using GLFW, and a simple GUI integration using ImGui.

## Requirements
This project is not limited to any GPU or OS. However, your GPU must meet the following requirements:
- Must support the following [extensions](#extensions):

  ```
  VK_KHR_ray_tracing_pipeline
  VK_KHR_acceleration_structure
  VK_KHR_swapchain
  VK_KHR_deferred_host_operations
  VK_KHR_buffer_device_address
  ```

- Must support Vulkan API version 1.4. This can be reduced if needed. However, if your GPU supports the above extensions, it should support Vulkan API version 1.4.

- Must have a driver capable of Vulkan ray tracing support. If you encounter issues using a supported GPU, update your driver.

## Dependencies


## Building

TODO: score GPU when picking device.

### Extensions
Extension support can be viewed on [Vulkan GPU Info](https://vulkan.gpuinfo.org/listextensions.php). In general, for this project, this includes NVIDIA GTX 10-series, AMD RX 6000-series, and the Intel Arc A-series and later. Many non-  desktop GPUs from a similar timeframe are also supported, such as integrated and workstation GPUs.
