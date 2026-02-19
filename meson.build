project('vkrt', ['c', 'cpp'],
  version: '1.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

cc = meson.get_compiler('c')
c_args = ['-D_GNU_SOURCE']


glslc = find_program('glslc', required: true)

# ----- Includes -----
includes = include_directories(
  'external/glfw3/include',
  'external/imgui/include',
  'external/imgui/icons',
  'external/cglm/include',
  'external/cgltf/include',
  'external/tinyfiledialogs',
  'src/core/api',
  'src/core/runtime',
  'src/core/render',
  'src/core/scene',
  'src/core/util',
  'src/app',
  'src/app/state',
  'src/app/scene',
  'src/app/ui',
  is_system: true
)

# ----- Dependencies -----
dependencies = [
  dependency('vulkan', required: true),
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
  cpp_args: ['-w']
)

# ----- VKRT core library -----
vkrt_sources = files(
  'src/core/runtime/buffer.c',
  'src/core/runtime/command.c',
  'src/core/render/descriptor.c',
  'src/core/runtime/device.c',
  'src/core/runtime/instance.c',
  'src/core/render/pipeline.c',
  'src/core/scene/scene.c',
  'src/core/render/structure.c',
  'src/core/runtime/surface.c',
  'src/core/runtime/swapchain.c',
  'src/core/runtime/validation.c',
  'src/core/api/vkrt.c',
  'src/core/util/io.c',
)

vkrt = static_library('vkrt_core',
  c_args: c_args,
  sources: vkrt_sources,
  dependencies: dependencies,
  include_directories: includes,
  link_with: [dcimgui],
  link_language: 'cpp'
)

# ----- App executable -----
executable('vkrt',
  c_args: c_args,
  sources: files(
    'src/app/main.c',
    'src/app/app.c',
    'src/app/state/editor_state.c',
    'src/app/scene/mesh_asset_loader.c',
    'src/app/scene/scene_controller.c',
    'src/app/ui/editor_theme.c',
    'src/app/ui/editor_ui.c',
    'external/tinyfiledialogs/tinyfiledialogs.c',
  ),
  dependencies: dependencies,
  include_directories: includes,
  link_with: [vkrt, dcimgui],
  link_language: 'cpp'
)


# ----- Shader sources -----
shader_sources = [
  'shaders/main.rchit',
  'shaders/main.rgen',
  'shaders/main.rmiss'
]

# ----- Shader targets -----
shader_targets = []
foreach shader_file : shader_sources
    basename = shader_file.split('.')[-1]
    shader_target = custom_target(
        'compile_' + basename,
        input: shader_file,
        output: basename + '.spv',
        command: [glslc, '@INPUT@', '--target-env=vulkan1.4', '-O', '-o', '@OUTPUT@'],
        build_by_default: true,
    )
    shader_targets += shader_target
endforeach
