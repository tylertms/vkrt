project('vkrt', ['c', 'cpp'],
  version: '1.0',
  meson_version: '>=1.6.0',
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
fs = import('fs')
python = import('python').find_installation()
embed_data = files('scripts/embed_data.py')
clang_tidy = find_program('clang-tidy', required: get_option('static_analysis'))

buildtype = get_option('buildtype')
profiling_enabled = get_option('profiling')
linux_window_backend = get_option('linux_window_backend')

c_args = []
cpp_args = []
app_link_args = []
if is_windows and cc.get_id() == 'gcc'
  app_link_args += ['-static']
endif
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
  'external/imgui/include',
  'external/cglm/include',
  'external/cgltf/include',
  'external/cjson',
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
  'src/app/scene',
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

subdir('external')

subdir('src')

subdir('external/nfd')

app_c_args = c_args + [
  c_define_prefix + 'VKRT_GLFW_X11=' + (glfw_build_x11 ? '1' : '0'),
  c_define_prefix + 'VKRT_GLFW_WAYLAND=' + (glfw_build_wayland ? '1' : '0'),
] + [c_define_prefix + 'VKRT_FILE_DIALOGS_ENABLED=' + (file_dialogs_enabled ? '1' : '0')]

executable('vkrt',
  c_args: app_c_args,
  link_args: app_link_args,
  sources: [app_sources, app_dialog_source, embedded_shader_sources],
  dependencies: app_dependencies,
  include_directories: app_includes,
  link_with: app_link_with,
)

configure_file(
  input: 'scripts/release.json.in',
  output: 'release.json',
  configuration: {
    'OIDN_ROOT': oidn_project.get_variable('oidn_root').replace('\\', '/'),
    'PLATFORM': sys,
    'CPU': host_machine.cpu_family(),
  },
)

foreach oidn_runtime_file : oidn_runtime_files
  configure_file(
    input: oidn_runtime_file,
    output: fs.name(oidn_runtime_file),
    copy: true,
  )
endforeach

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

if clang_tidy.found()
  foreach mode : ['', '-fix']
    run_target('clang-tidy' + mode,
      command: [python, files('scripts/run_clang_tidy.py'), '--build-dir', meson.project_build_root()] + (mode == '-fix' ? ['--fix'] : []),
      env: {
        'CLANG_TIDY': clang_tidy.full_path(),
      },
    )
  endforeach
endif
