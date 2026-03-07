project('vkrt', ['c', 'cpp'],
  version: '1.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

sys = host_machine.system()
if sys == 'darwin'
  add_languages('objc', native: false, required: true)
endif

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

project_includes = include_directories(
  'shared',
  'assets/fonts',
)

external_includes = include_directories(
  'external/glfw3/include',
  'external/imgui/include',
  'external/imgui/icons',
  'external/cglm/include',
  'external/cgltf/include',
  'external/spng',
  'external/nfd/src/include',
  is_system: true,
)

core_includes = include_directories(
  'src/core/api',
  'src/core/runtime',
  'src/core/render',
  'src/core/scene',
  'src/core/utility',
)

app_local_includes = include_directories(
  'src/app',
  'src/app/session',
  'src/app/mesh',
  'src/app/render',
  'src/app/editor',
)

app_core_includes = include_directories(
  'src/core/api',
  'src/core/utility',
)

app_includes = [
  project_includes,
  external_includes,
  app_local_includes,
  app_core_includes,
]

core_build_includes = [
  project_includes,
  external_includes,
  core_includes,
]

core_internal_includes = include_directories(
  'src/core/internal',
)

vulkan_dep = dependency('vulkan', required: true)
glfw_dep = dependency('glfw3', static: true)
zlib_dep = dependency('zlib', required: true)
threads_dep = dependency('threads')
rt_dep = dependency('rt', required: false)

core_dependencies = [
  vulkan_dep,
  glfw_dep,
  zlib_dep,
  threads_dep,
  rt_dep,
]

app_dependencies = [
  vulkan_dep,
  glfw_dep,
  threads_dep,
  rt_dep,
]

imgui_dependencies = [
  vulkan_dep,
  glfw_dep,
]

nfd_source = []
nfd_deps = []
nfd_cpp_args = ['-w']
nfd_objc_args = ['-w']

if sys == 'darwin'
  nfd_deps += dependency(
    'appleframeworks',
    modules: ['AppKit'],
  )
  nfd_uti_dep = dependency(
    'appleframeworks',
    modules: ['UniformTypeIdentifiers'],
    required: false,
  )
  if nfd_uti_dep.found()
    nfd_deps += nfd_uti_dep
    nfd_objc_args += ['-DNFD_MACOS_ALLOWEDCONTENTTYPES=1']
  else
    nfd_objc_args += ['-DNFD_MACOS_ALLOWEDCONTENTTYPES=0']
  endif
  nfd_source = files('external/nfd/src/nfd_cocoa.m')
elif sys == 'windows'
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
  objc_args: nfd_objc_args,
)

dcimgui = static_library('dcimgui',
  include_directories: [external_includes],
  dependencies: imgui_dependencies,
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
  dependencies: app_dependencies,
  include_directories: app_includes,
  link_with: [vkrt, dcimgui, nfd],
)

subdir('shaders')
