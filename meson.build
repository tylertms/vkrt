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

buildtype = get_option('buildtype')
trace_enabled = buildtype == 'debug' or buildtype == 'debugoptimized'
if trace_enabled
  c_args += ['-DVKRT_TRACE_ENABLED=1']
else
  c_args += ['-DVKRT_TRACE_ENABLED=0']
endif

glslc = find_program('glslc', required: true)

includes = include_directories(
  'shared',
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

nfd_source = []
nfd_deps = []
nfd_cpp_args = ['-w']
sys = host_machine.system()

if sys == 'windows'
  nfd_source = files('external/nfd/src/nfd_win.cpp')
elif sys == 'linux'
  nfd_backend = get_option('nfd_backend')

  dbus_dep = dependency('dbus-1', required: false, method: 'pkg-config')
  gtk3_dep = dependency('gtk+-3.0', required: false, method: 'pkg-config')

  if nfd_backend == 'portal'
    if not dbus_dep.found()
      error('nfd_backend=portal requires dbus-1 development files')
    endif
    nfd_source = files('external/nfd/src/nfd_portal.cpp')
    nfd_deps += dbus_dep
    nfd_cpp_args += ['-DNFD_PORTAL']
  elif nfd_backend == 'gtk'
    if not gtk3_dep.found()
      error('nfd_backend=gtk requires gtk+-3.0 development files')
    endif
    nfd_source = files('external/nfd/src/nfd_gtk.cpp')
    nfd_deps += gtk3_dep
  elif nfd_backend == 'auto'
    if dbus_dep.found()
      nfd_source = files('external/nfd/src/nfd_portal.cpp')
      nfd_deps += dbus_dep
      nfd_cpp_args += ['-DNFD_PORTAL']
      message('NFD backend: portal (dbus-1)')
    elif gtk3_dep.found()
      nfd_source = files('external/nfd/src/nfd_gtk.cpp')
      nfd_deps += gtk3_dep
      message('NFD backend: gtk (gtk+-3.0)')
    else
      error('No Linux NFD backend found. Install dbus-1 development files (preferred) or gtk+-3.0 development files.')
    endif
  else
    error('Invalid nfd_backend option: ' + nfd_backend)
  endif
else
  error('Unsupported platform for nfd')
endif

nfd = static_library('nfd',
  include_directories: include_directories('external/nfd/src/include'),
  sources: nfd_source,
  dependencies: nfd_deps,
  cpp_args: nfd_cpp_args,
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

executable('vkrt',
  c_args: c_args,
  sources: app_sources,
  dependencies: dependencies,
  include_directories: includes,
  link_with: [vkrt, dcimgui, nfd],
  link_language: 'cpp',
)

subdir('shaders')
