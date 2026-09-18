vulkan_dep = dependency('vulkan', version: '>=1.4.341', required: true)
windows_system_deps = []
darwin_system_deps = []

if is_windows
  windows_system_deps += cc.find_library('dwmapi', required: true)
endif

if is_darwin
  darwin_system_deps += dependency(
    'appleframeworks',
    modules: [
      'Cocoa',
      'CoreFoundation',
      'IOKit',
      'QuartzCore',
    ],
  )
endif

glfw_build_win32 = is_windows
glfw_build_cocoa = is_darwin
glfw_build_x11 = is_linux and linux_window_backend in ['x11', 'both']
glfw_build_wayland = is_linux and linux_window_backend in ['wayland', 'both']
if glfw_build_wayland
  linux_wayland_scanner = find_program('wayland-scanner')
endif

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

threads_dep = dependency('threads')
rt_dep = dependency('rt', required: false)
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
glfw_dep = glfw_project.dependency('glfw').as_system()

zlib_project = subproject('zlib', default_options: ['default_library=static', 'warning_level=0', 'c_std=gnu11,c11'])
zlib_dep = zlib_project.get_variable('zlib_dep').as_system()
spng_project = subproject('spng', default_options: [
  'default_library=static',
  'static_zlib=false',
  'build_examples=false',
  'warning_level=0',
])
spng_dep = spng_project.get_variable('spng_dep').as_system()
turbojpeg_project = subproject('libjpeg-turbo', default_options: [
  'c_std=gnu11,c11',
  'default_library=static',
  'tests=disabled',
  'warning_level=0',
])
turbojpeg_dep = turbojpeg_project.get_variable('turbojpeg_dep').as_system()

oidn_runtime_files = []
oidn_subproject_name = ''
oidn_cpu = host_machine.cpu_family()

if is_windows and oidn_cpu == 'x86_64'
  oidn_subproject_name = 'oidn_windows_x86_64'
elif is_linux and oidn_cpu == 'x86_64'
  oidn_subproject_name = 'oidn_linux_x86_64'
elif is_darwin and oidn_cpu == 'aarch64'
  oidn_subproject_name = 'oidn_macos_arm64'
elif is_darwin and oidn_cpu == 'x86_64'
  oidn_subproject_name = 'oidn_macos_x86_64'
else
  error('OIDN is required but no packaged build is available for ' + sys + ' ' + oidn_cpu)
endif

oidn_project = subproject(oidn_subproject_name, required: true)
oidn_dep = oidn_project.get_variable('oidn_dep')
oidn_runtime_files = oidn_project.get_variable('oidn_runtime_files')
oidn_version = oidn_project.get_variable('oidn_version')

c_args += [c_define_prefix + 'VKRT_GLFW_VERSION="' + glfw_dep.version() + '"']
c_args += [c_define_prefix + 'VKRT_SPNG_VERSION="' + spng_dep.version() + '"']
c_args += [c_define_prefix + 'VKRT_TURBOJPEG_VERSION="' + turbojpeg_dep.version() + '"']
c_args += [c_define_prefix + 'VKRT_OIDN_VERSION="' + oidn_version + '"']

core_cpp_args = cpp_args
if is_windows and cpp_is_msvc
  core_cpp_args += ['/wd4245', '/wd4702']
endif

core_dependencies = [
  vulkan_dep,
  glfw_dep,
  threads_dep,
  rt_dep,
  spng_dep,
  turbojpeg_dep,
  zlib_dep,
  windows_system_deps,
  darwin_system_deps,
]

core_dependencies += [oidn_dep]

app_dependencies = [
  vulkan_dep,
  glfw_dep,
  threads_dep,
  rt_dep,
  windows_system_deps,
  darwin_system_deps,
]

imgui_dependencies = [
  vulkan_dep,
  glfw_dep,
]

imgui_cpp_args = cpp_args
if is_linux
  if not glfw_build_x11
    imgui_cpp_args += ['-DIMGUI_IMPL_GLFW_DISABLE_X11']
  endif
  if not glfw_build_wayland
    imgui_cpp_args += ['-DIMGUI_IMPL_GLFW_DISABLE_WAYLAND']
  endif
endif

dcimgui = static_library('dcimgui',
  include_directories: [external_includes],
  dependencies: imgui_dependencies,
  cpp_args: imgui_cpp_args,
  sources: files(
    'imgui/src/dcimgui.cpp',
    'imgui/src/dcimgui_impl_glfw.cpp',
    'imgui/src/dcimgui_impl_vulkan.cpp',
    'imgui/src/dcimgui_internal.cpp',
    'imgui/src/imgui.cpp',
    'imgui/src/imgui_demo.cpp',
    'imgui/src/imgui_draw.cpp',
    'imgui/src/imgui_impl_glfw.cpp',
    'imgui/src/imgui_impl_vulkan.cpp',
    'imgui/src/imgui_tables.cpp',
    'imgui/src/imgui_widgets.cpp',
  ),
  override_options: ['warning_level=0'],
)

cjson = static_library('cjson',
  include_directories: include_directories('cjson'),
  sources: files('cjson/cJSON.c'),
  override_options: ['warning_level=0'],
)
