project('vkrt', ['c', 'cpp'],
  version: '1.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

sys = host_machine.system()
is_linux = sys == 'linux'
is_windows = sys == 'windows'
is_darwin = sys == 'darwin'

if not is_linux and not is_windows and not is_darwin
  error('Unsupported host system: ' + sys)
endif

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
cmake = import('cmake')

buildtype = get_option('buildtype')
profiling_enabled = get_option('profiling')
linux_window_backend = get_option('linux_window_backend')

c_args = []
cpp_args = []
app_link_args = []
cc_is_msvc = cc.get_argument_syntax() == 'msvc'
cpp_is_msvc = cpp.get_argument_syntax() == 'msvc'
is_debug_build = buildtype == 'debug' or buildtype == 'debugoptimized'
c_define_prefix = cc_is_msvc ? '/D' : '-D'
cpp_define_prefix = cpp_is_msvc ? '/D' : '-D'

if is_linux
  c_args += [
    c_define_prefix + '_GNU_SOURCE',
    c_define_prefix + '_POSIX_C_SOURCE=200112L',
  ]
  cpp_args += [
    cpp_define_prefix + '_GNU_SOURCE',
    cpp_define_prefix + '_POSIX_C_SOURCE=200112L',
  ]
endif

if is_windows
  c_args += [c_define_prefix + '_CRT_SECURE_NO_WARNINGS']
  cpp_args += [cpp_define_prefix + '_CRT_SECURE_NO_WARNINGS']
endif

c_args += [c_define_prefix + 'VKRT_VERSION="' + get_option('vkrt_version') + '"']
c_args += [c_define_prefix + 'GLFW_INCLUDE_NONE']
cpp_args += [cpp_define_prefix + 'GLFW_INCLUDE_NONE']

validation_enabled = is_debug_build and not profiling_enabled
debug_utils_enabled = validation_enabled or profiling_enabled
trace_enabled = is_debug_build
if trace_enabled
  c_args += [c_define_prefix + 'VKRT_TRACE_ENABLED=1']
else
  c_args += [c_define_prefix + 'VKRT_TRACE_ENABLED=0']
endif
c_args += [c_define_prefix + 'VKRT_PROFILING_ENABLED=' + (profiling_enabled ? '1' : '0')]
c_args += [c_define_prefix + 'VKRT_VALIDATION_ENABLED=' + (validation_enabled ? '1' : '0')]
c_args += [c_define_prefix + 'VKRT_DEBUG_UTILS_ENABLED=' + (debug_utils_enabled ? '1' : '0')]
cpp_args += [cpp_define_prefix + 'VKRT_PROFILING_ENABLED=' + (profiling_enabled ? '1' : '0')]
cpp_args += [cpp_define_prefix + 'VKRT_VALIDATION_ENABLED=' + (validation_enabled ? '1' : '0')]
cpp_args += [cpp_define_prefix + 'VKRT_DEBUG_UTILS_ENABLED=' + (debug_utils_enabled ? '1' : '0')]

if profiling_enabled
  if cc_is_msvc
    c_args += ['/Zi', '/Oy-']
  else
    c_args += ['-g', '-fno-omit-frame-pointer']
  endif

  if cpp_is_msvc
    cpp_args += ['/Zi', '/Oy-']
  else
    cpp_args += ['-g', '-fno-omit-frame-pointer']
  endif

  if cc_is_msvc or cpp_is_msvc
    app_link_args += ['/DEBUG']
  else
    app_link_args += ['-g']
  endif
endif

slang_compile_args = ['-O3']
if profiling_enabled
  slang_compile_args += ['-g']
endif

slangc = find_program('slangc', required: true)

project_includes = include_directories(
  'assets/fonts',
  'src/shared',
)

external_includes = include_directories(
  'external/glfw3/include',
  'external/imgui/include',
  'external/imgui/icons',
  'external/cglm/include',
  'external/cgltf/include',
  'external/nfd/src/include',
  'external/tinyexr',
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
  app_local_includes,
  app_core_includes,
  external_includes,
]

core_build_includes = [
  project_includes,
  core_includes,
  external_includes,
]

core_internal_includes = include_directories(
  'src/core/internal',
)

vulkan_dep = dependency('vulkan', required: true)

linux_glfw_backend = 'auto'
linux_x11_probe_deps = []
linux_wayland_probe_deps = []
linux_wayland_scanner = disabler()
linux_has_x11 = false
linux_has_wayland = false

if is_linux
  linux_x11_probe_deps = [
    dependency('x11', required: false, method: 'pkg-config'),
    dependency('xrandr', required: false, method: 'pkg-config'),
    dependency('xinerama', required: false, method: 'pkg-config'),
    dependency('xcursor', required: false, method: 'pkg-config'),
    dependency('xi', required: false, method: 'pkg-config'),
    dependency('xext', required: false, method: 'pkg-config'),
  ]
  linux_wayland_probe_deps = [
    dependency('wayland-client', required: false, method: 'pkg-config'),
    dependency('wayland-cursor', required: false, method: 'pkg-config'),
    dependency('wayland-egl', required: false, method: 'pkg-config'),
    dependency('xkbcommon', required: false, method: 'pkg-config'),
  ]
  linux_wayland_scanner = find_program('wayland-scanner', required: false, native: true)

  linux_has_x11 = true
  foreach dep : linux_x11_probe_deps
    linux_has_x11 = linux_has_x11 and dep.found()
  endforeach

  linux_has_wayland = linux_wayland_scanner.found()
  foreach dep : linux_wayland_probe_deps
    linux_has_wayland = linux_has_wayland and dep.found()
  endforeach

  if linux_window_backend == 'auto'
    if linux_has_x11 and linux_has_wayland
      linux_glfw_backend = 'both'
    elif linux_has_wayland
      linux_glfw_backend = 'wayland'
    elif linux_has_x11
      linux_glfw_backend = 'x11'
    else
      error('No supported Linux GLFW backend was detected. Install X11 development files, or Wayland development files plus wayland-scanner, or set -Dlinux_window_backend explicitly.')
    endif
  else
    linux_glfw_backend = linux_window_backend
  endif

  if linux_glfw_backend == 'x11'
    if not linux_has_x11
      error('linux_window_backend=x11 requires x11, xrandr, xinerama, xcursor, xi, and xext development files.')
    endif
  elif linux_glfw_backend == 'wayland'
    if not linux_has_wayland
      error('linux_window_backend=wayland requires wayland-client, wayland-cursor, wayland-egl, xkbcommon, and wayland-scanner.')
    endif
  elif linux_glfw_backend == 'both'
    if not linux_has_x11 or not linux_has_wayland
      error('linux_window_backend=both requires both the X11 and Wayland development stacks, plus wayland-scanner.')
    endif
  else
    error('Invalid linux_window_backend option: ' + linux_glfw_backend)
  endif
elif linux_window_backend != 'auto'
  message('Ignoring linux_window_backend=' + linux_window_backend + ' on ' + sys)
endif

glfw_build_win32 = is_windows
glfw_build_cocoa = is_darwin
glfw_build_x11 = is_linux and (linux_glfw_backend == 'x11' or linux_glfw_backend == 'both')
glfw_build_wayland = is_linux and (linux_glfw_backend == 'wayland' or linux_glfw_backend == 'both')

glfw_msvc_runtime = ''
if cc_is_msvc or cpp_is_msvc
  vscrt = get_option('b_vscrt')
  if vscrt == 'from_buildtype'
    glfw_msvc_runtime = buildtype == 'debug' ? 'MultiThreadedDebugDLL' : 'MultiThreadedDLL'
  elif vscrt == 'static_from_buildtype'
    glfw_msvc_runtime = buildtype == 'debug' ? 'MultiThreadedDebug' : 'MultiThreaded'
  elif vscrt == 'md'
    glfw_msvc_runtime = 'MultiThreadedDLL'
  elif vscrt == 'mdd'
    glfw_msvc_runtime = 'MultiThreadedDebugDLL'
  elif vscrt == 'mt'
    glfw_msvc_runtime = 'MultiThreaded'
  elif vscrt == 'mtd'
    glfw_msvc_runtime = 'MultiThreadedDebug'
  endif
endif

glfw_options = cmake.subproject_options()
glfw_options.set_override_option('warning_level', '0')
glfw_options.add_cmake_defines({
  'BUILD_SHARED_LIBS': 'OFF',
  'GLFW_BUILD_DOCS': 'OFF',
  'GLFW_BUILD_EXAMPLES': 'OFF',
  'GLFW_BUILD_TESTS': 'OFF',
  'GLFW_INSTALL': 'OFF',
  'GLFW_BUILD_WIN32': glfw_build_win32 ? 'ON' : 'OFF',
  'GLFW_BUILD_COCOA': glfw_build_cocoa ? 'ON' : 'OFF',
  'GLFW_BUILD_X11': glfw_build_x11 ? 'ON' : 'OFF',
  'GLFW_BUILD_WAYLAND': glfw_build_wayland ? 'ON' : 'OFF',
  'GLFW_ENABLE_WARNINGS': 'OFF',
  'CMAKE_SUPPRESS_DEVELOPER_WARNINGS': 'TRUE',
})
if glfw_msvc_runtime != ''
  glfw_options.add_cmake_defines({
    'CMAKE_MSVC_RUNTIME_LIBRARY': glfw_msvc_runtime,
  })
endif
glfw_project = cmake.subproject('glfw', options: glfw_options)
glfw_dep = glfw_project.dependency('glfw')

threads_dep = dependency('threads')
rt_dep = dependency('rt', required: false)
zlib_project = subproject('zlib', default_options: ['default_library=static'])
zlib_dep = zlib_project.get_variable('zlib_dep')

spng_project = subproject(
  'spng',
  default_options: [
    'default_library=static',
    'static_zlib=true',
    'build_examples=false',
    'warning_level=0',
  ]
)
spng_dep = spng_project.get_variable('spng_dep')

turbojpeg_project = subproject(
  'libjpeg-turbo',
  default_options: {
    'default_library': 'static',
    'tests': 'disabled',
    'warning_level': '0',
    'c_args': is_linux ? ['-D_DEFAULT_SOURCE', '-include', 'strings.h'] : [],
  }
)
turbojpeg_dep = turbojpeg_project.get_variable('turbojpeg_dep')

core_cpp_args = cpp_args
if is_windows and cpp_is_msvc
  core_cpp_args += ['/wd4245', '/wd4702']
endif

c_args += [c_define_prefix + 'VKRT_GLFW_VERSION="' + glfw_dep.version() + '"']
c_args += [c_define_prefix + 'VKRT_SPNG_VERSION="' + spng_dep.version() + '"']
c_args += [c_define_prefix + 'VKRT_TURBOJPEG_VERSION="' + turbojpeg_dep.version() + '"']

core_dependencies = [
  vulkan_dep,
  glfw_dep,
  threads_dep,
  rt_dep,
  spng_dep,
  turbojpeg_dep,
  zlib_dep,
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

dcimgui = static_library('dcimgui',
  include_directories: [external_includes],
  dependencies: imgui_dependencies,
  cpp_args: cpp_args,
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
  override_options: ['warning_level=0'],
)

subdir('src')

file_dialogs_opt = get_option('file_dialogs')
file_dialogs_enabled = false
file_dialogs_backend = 'disabled'
app_dialog_source = files('src/app/editor/dialogs_stub.c')
app_link_with = [vkrt, dcimgui]

if not file_dialogs_opt.disabled()
  nfd_source = []
  nfd_deps = []
  nfd_cpp_args = []

  if is_windows
    nfd_source = files('external/nfd/src/nfd_win.cpp')
    file_dialogs_enabled = true
    file_dialogs_backend = 'win32'
  elif is_linux
    nfd_backend = get_option('nfd_backend')

    dbus_dep = dependency('dbus-1', required: false, method: 'pkg-config')
    gtk3_dep = dependency('gtk+-3.0', required: false, method: 'pkg-config')

    if nfd_backend == 'portal'
      if not dbus_dep.found()
        if file_dialogs_opt.enabled()
          error('file_dialogs enabled with nfd_backend=portal requires dbus-1 development files')
        endif
      else
        nfd_source = files('external/nfd/src/nfd_portal.cpp')
        nfd_deps += dbus_dep
        nfd_cpp_args += ['-DNFD_PORTAL']
        file_dialogs_enabled = true
        file_dialogs_backend = 'portal'
      endif
    elif nfd_backend == 'gtk'
      if not gtk3_dep.found()
        if file_dialogs_opt.enabled()
          error('file_dialogs enabled with nfd_backend=gtk requires gtk+-3.0 development files')
        endif
      else
        nfd_source = files('external/nfd/src/nfd_gtk.cpp')
        nfd_deps += gtk3_dep
        file_dialogs_enabled = true
        file_dialogs_backend = 'gtk'
      endif
    elif nfd_backend == 'auto'
      if dbus_dep.found()
        nfd_source = files('external/nfd/src/nfd_portal.cpp')
        nfd_deps += dbus_dep
        nfd_cpp_args += ['-DNFD_PORTAL']
        file_dialogs_enabled = true
        file_dialogs_backend = 'portal'
        message('NFD backend: portal (dbus-1)')
      elif gtk3_dep.found()
        nfd_source = files('external/nfd/src/nfd_gtk.cpp')
        nfd_deps += gtk3_dep
        file_dialogs_enabled = true
        file_dialogs_backend = 'gtk'
        message('NFD backend: gtk (gtk+-3.0)')
      elif file_dialogs_opt.enabled()
        error('file_dialogs enabled but no Linux NFD backend was found. Install dbus-1 or gtk+-3.0 development files.')
      endif
    else
      error('Invalid nfd_backend option: ' + nfd_backend)
    endif
  elif file_dialogs_opt.enabled()
    error('file_dialogs is currently only supported by this Meson build on Windows and Linux.')
  endif

  if file_dialogs_enabled
    nfd = static_library('nfd',
      include_directories: include_directories('external/nfd/src/include'),
      sources: nfd_source,
      dependencies: nfd_deps,
      cpp_args: nfd_cpp_args,
      override_options: ['warning_level=0'],
    )
    app_dialog_source = files('src/app/editor/dialogs.c')
    app_link_with += [nfd]
  else
    message('Native file dialogs disabled for this build')
  endif
else
  message('Native file dialogs disabled by option')
endif

app_c_args = c_args + [c_define_prefix + 'VKRT_FILE_DIALOGS_ENABLED=' + (file_dialogs_enabled ? '1' : '0')]

executable('vkrt',
  c_args: app_c_args,
  link_args: app_link_args,
  sources: [app_sources, app_dialog_source, embedded_shader_sources],
  dependencies: app_dependencies,
  include_directories: app_includes,
  link_with: app_link_with,
)

glfw_backend_summary = 'unknown'
if glfw_build_win32
  glfw_backend_summary = 'win32'
elif glfw_build_cocoa
  glfw_backend_summary = 'cocoa'
elif glfw_build_x11 and glfw_build_wayland
  glfw_backend_summary = 'x11 + wayland'
elif glfw_build_x11
  glfw_backend_summary = 'x11'
elif glfw_build_wayland
  glfw_backend_summary = 'wayland'
endif

summary({
  'Host system': sys,
  'GLFW backend': glfw_backend_summary,
  'File dialogs': file_dialogs_backend,
  'Profiling': profiling_enabled,
}, section: 'vkrt')
