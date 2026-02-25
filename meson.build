project('vkspt', ['c', 'cpp'],
  version: '1.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

cc = meson.get_compiler('c')
c_args = ['-D_GNU_SOURCE']

buildtype = get_option('buildtype')
trace_enabled = buildtype == 'debug' or buildtype == 'debugoptimized'
if trace_enabled
  c_args += ['-DVKRT_TRACE_ENABLED=1']
else
  c_args += ['-DVKRT_TRACE_ENABLED=0']
endif

glslc = find_program('glslc', required: true)

includes = include_directories(
  'external/glfw3/include',
  'external/imgui/include',
  'external/imgui/icons',
  'external/cglm/include',
  'external/cgltf/include',
  'external/spng',
  'external/nfd/src/include',
  'assets/fonts',
  'src/core/api',
  'src/core/runtime',
  'src/core/render',
  'src/core/scene',
  'src/core/utility',
  'src/app',
  'src/app/session',
  'src/app/mesh',
  'src/app/ui',
  is_system: true,
)

dependencies = [
  dependency('vulkan', required: true),
  dependency('glfw3', static: true),
  dependency('zlib', required: true),
  dependency('threads'),
  dependency('rt', required: false),
]

nfd = static_library('nfd',
  include_directories: include_directories('external/nfd/src/include'),
  dependencies: [
    cc.find_library('ole32'),
    cc.find_library('uuid'),
    cc.find_library('shell32'),
  ],
  sources: files('external/nfd/src/nfd_win.cpp'),
  cpp_args: ['-w'],
)

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
  cpp_args: ['-w'],
)

subdir('src')

executable('vkspt',
  c_args: c_args,
  sources: app_sources,
  dependencies: dependencies,
  include_directories: includes,
  link_with: [vkrt, dcimgui, nfd],
  link_language: 'cpp',
)

subdir('shaders')
