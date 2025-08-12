project('vkrt', ['c', 'cpp'],
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=2',
  ],
)

# ----- Includes -----
incs = include_directories(
  'include',
  'external/glfw3/include',
  'external/imgui/include',
  'external/cglm/include',
  'external/cgltf/include',
  is_system: true
)

# ----- Dependencies -----
deps = [
  dependency('vulkan'),
  dependency('glfw3', static: true),
  dependency('rt', required: false), # ignored on platforms without librt
]

# ----- Dear ImGui bridge (static) -----
dcimgui = static_library('dcimgui',
  include_directories: incs,
  dependencies: deps,
  sources: files(
    'external/imgui/src/dcimgui.cpp',
    'external/imgui/src/dcimgui_impl_glfw.cpp',
    'external/imgui/src/dcimgui_impl_vulkan.cpp',
    'external/imgui/src/dcimgui_internal.cpp',
    'external/imgui/src/imgui.cpp',
    'external/imgui/src/imgui_demo.cpp',
    'external/imgui/src/imgui_draw.cpp',
    'external/imgui/src/imgui_impl_glfw.cpp',
    'external/imgui/src/imgui_impl_vulkan.cpp',
    'external/imgui/src/imgui_tables.cpp',
    'external/imgui/src/imgui_widgets.cpp',
  ),
  install: true
)

# ----- VKRT sources -----
vkrt_src = files(
  'src/app.c',
  'src/buffer.c',
  'src/command.c',
  'src/descriptor.c',
  'src/device.c',
  'src/instance.c',
  'src/interface.c',
  'src/object.c',
  'src/pipeline.c',
  'src/structure.c',
  'src/surface.c',
  'src/swapchain.c',
  'src/validation.c',
  'src/vkrt.c',
)

vkrtlib = static_library('vkrt',
  vkrt_src,
  c_args: ['-D_GNU_SOURCE'],
  include_directories: incs,
  dependencies: deps,
  link_with: dcimgui,
  link_language: 'cpp',
  install: true
)

vkrt_dep = declare_dependency(
  link_with: [vkrtlib, dcimgui],
  include_directories: incs,
  dependencies: deps
)

meson.override_dependency('vkrt', vkrt_dep)

install_subdir('include', install_dir: get_option('includedir'))

pkgconfig = import('pkgconfig')
pkgconfig.generate(
  name: 'vkrt',
  description: 'Vulkan ray tracing toolkit',
  version: meson.project_version(),
  libraries: vkrtlib,
  subdirs: 'include'
)