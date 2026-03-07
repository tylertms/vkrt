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
cpp = meson.get_compiler('cpp')
cmake = import('cmake')

c_args = []
cpp_args = []
define_prefix = cc.get_argument_syntax() == 'msvc' ? '/D' : '-D'

if sys == 'linux' and cc.get_argument_syntax() == 'gcc'
  c_args += [define_prefix + '_GNU_SOURCE']
endif

c_args += [define_prefix + 'VKRT_VERSION="' + meson.project_version() + '"']
c_args += [define_prefix + 'GLFW_INCLUDE_NONE']
cpp_args += [define_prefix + 'GLFW_INCLUDE_NONE']

buildtype = get_option('buildtype')
trace_enabled = buildtype == 'debug' or buildtype == 'debugoptimized'
if trace_enabled
  c_args += [define_prefix + 'VKRT_TRACE_ENABLED=1']
else
  c_args += [define_prefix + 'VKRT_TRACE_ENABLED=0']
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
  app_local_includes,
  app_core_includes,
  external_includes,
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

glfw_dep = dependency('glfw3', required: false, allow_fallback: false)
if not glfw_dep.found()
  glfw_options = cmake.subproject_options()
  glfw_options.append_compile_args('c', cc.get_argument_syntax() == 'msvc' ? '/w' : '-w')
  if sys == 'darwin'
    glfw_options.append_compile_args('objc', '-w')
  endif
  glfw_options.add_cmake_defines({
    'BUILD_SHARED_LIBS': 'OFF',
    'GLFW_BUILD_DOCS': 'OFF',
    'GLFW_BUILD_EXAMPLES': 'OFF',
    'GLFW_BUILD_TESTS': 'OFF',
    'GLFW_INSTALL': 'OFF',
    'GLFW_BUILD_WAYLAND': 'ON',
    'CMAKE_MSVC_RUNTIME_LIBRARY': 'MultiThreaded',
  })
  glfw_project = cmake.subproject('glfw', options: glfw_options)
  glfw_dep = glfw_project.dependency('glfw')
endif

if sys == 'windows'
  zlib_dep = dependency(
    'zlib',
    required: true,
    fallback: ['zlib', 'zlib_dep'],
    default_options: ['default_library=static', 'warning_level=0'],
  )
else
  zlib_dep = dependency('zlib', required: false, allow_fallback: false)
  if not zlib_dep.found()
    zlib_dep = dependency(
      'zlib',
      required: true,
      fallback: ['zlib', 'zlib_dep'],
      default_options: ['default_library=static', 'warning_level=0'],
    )
  endif
endif

threads_dep = dependency('threads')
rt_dep = dependency('rt', required: false)

if sys == 'darwin'
  iokit_dep = dependency('appleframeworks', modules: ['IOKit'])
endif

core_dependencies = [
  vulkan_dep,
  glfw_dep,
  threads_dep,
  rt_dep,
]

app_dependencies = [
  vulkan_dep,
  glfw_dep,
  threads_dep,
  rt_dep,
]

if sys == 'darwin'
  core_dependencies += iokit_dep
  app_dependencies += iokit_dep
endif

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
app_dialog_source = files('src/app/editor/dialogs_stub.c')
app_link_with = [vkrt, dcimgui]

if not file_dialogs_opt.disabled()
  nfd_source = []
  nfd_deps = []
  nfd_cpp_args = []
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
    file_dialogs_enabled = true
  elif sys == 'windows'
    nfd_source = files('external/nfd/src/nfd_win.cpp')
    file_dialogs_enabled = true
  elif sys == 'linux'
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
      endif
    elif nfd_backend == 'auto'
      if dbus_dep.found()
        nfd_source = files('external/nfd/src/nfd_portal.cpp')
        nfd_deps += dbus_dep
        nfd_cpp_args += ['-DNFD_PORTAL']
        file_dialogs_enabled = true
        message('NFD backend: portal (dbus-1)')
      elif gtk3_dep.found()
        nfd_source = files('external/nfd/src/nfd_gtk.cpp')
        nfd_deps += gtk3_dep
        file_dialogs_enabled = true
        message('NFD backend: gtk (gtk+-3.0)')
      elif file_dialogs_opt.enabled()
        error('file_dialogs enabled but no Linux NFD backend was found. Install dbus-1 or gtk+-3.0 development files.')
      endif
    else
      error('Invalid nfd_backend option: ' + nfd_backend)
    endif
  endif

  if file_dialogs_enabled
    nfd = static_library('nfd',
      include_directories: include_directories('external/nfd/src/include'),
      sources: nfd_source,
      dependencies: nfd_deps,
      cpp_args: nfd_cpp_args,
      objc_args: nfd_objc_args,
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

app_c_args = c_args + [define_prefix + 'VKRT_FILE_DIALOGS_ENABLED=' + (file_dialogs_enabled ? '1' : '0')]

executable('vkrt',
  c_args: app_c_args,
  sources: [app_sources, app_dialog_source, embedded_shader_sources],
  dependencies: app_dependencies,
  include_directories: app_includes,
  link_with: app_link_with,
)
