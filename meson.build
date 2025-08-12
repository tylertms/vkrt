project('vkrt', ['c', 'cpp'],
  version: '1.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')
c_args = ['-D_GNU_SOURCE']

glslc = find_program('glslc', required: true)

# ----- Includes -----
includes = include_directories(
  'external/glfw3/include',
  'external/imgui/include',
  'external/cglm/include',
  'external/cgltf/include',
  is_system: true
)

# ----- Dependencies -----
dependencies = [
  dependency('vulkan'),
  dependency('glfw3', static: true),
  dependency('rt', required: false),
]

# ----- Dear ImGui bridge (static) -----
dcimgui = static_library('dcimgui',
  include_directories: includes,
  dependencies: dependencies,
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
sources = files(
  'src/app.c',
  'src/buffer.c',
  'src/command.c',
  'src/descriptor.c',
  'src/device.c',
  'src/gui.c',
  'src/instance.c',
  'src/main.c',
  'src/object.c',
  'src/pipeline.c',
  'src/scene.c',
  'src/structure.c',
  'src/surface.c',
  'src/swapchain.c',
  'src/validation.c',
  'src/vkrt.c',
)

# ----- VKRT shaders -----
shader_sources = files(
  'shaders/main.rchit',
  'shaders/main.rgen',
  'shaders/main.rmiss'
)

shader_targets = []
foreach shader_file : shader_sources
    basename = shader_file.full_path().split('.')[-1]
    shader_target = custom_target(
        'compile_' + basename,
        input: shader_file,
        output: basename + '.spv',
        command: [glslc, '@INPUT@', '--target-env=vulkan1.4', '-O', '-o', '@OUTPUT@'],
        build_by_default: true,
    )
    shader_targets += shader_target
endforeach


executable('vkrt',
  c_args: c_args,
  sources: sources,
  dependencies: dependencies,
  include_directories: includes,
  link_with: [dcimgui],
  link_language: 'cpp'
)