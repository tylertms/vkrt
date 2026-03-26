project(
  'oidn',
  'c',
  version: '2.4.1',
)

cc = meson.get_compiler('c')
sys = host_machine.system()
cpu = host_machine.cpu_family()

oidn_include = include_directories('include')
oidn_lib = cc.find_library('OpenImageDenoise', dirs: [meson.current_source_dir() / 'lib'], required: true)
oidn_runtime_files = []
oidn_link_args = []

if sys == 'windows' and cpu == 'x86_64'
  oidn_runtime_files = files(
    'bin/OpenImageDenoise.dll',
    'bin/OpenImageDenoise_core.dll',
    'bin/OpenImageDenoise_device_cpu.dll',
    'bin/tbb12.dll',
    'bin/tbbbind.dll',
    'bin/tbbbind_2_0.dll',
    'bin/tbbbind_2_5.dll',
  )
elif sys == 'linux' and cpu == 'x86_64'
  oidn_runtime_files = files(
    'lib/libOpenImageDenoise.so',
    'lib/libOpenImageDenoise.so.2',
    'lib/libOpenImageDenoise.so.2.4.1',
    'lib/libOpenImageDenoise_core.so.2.4.1',
    'lib/libOpenImageDenoise_device_cpu.so.2.4.1',
    'lib/libtbb.so',
    'lib/libtbb.so.12',
    'lib/libtbb.so.12.17',
    'lib/libtbbbind.so.3',
    'lib/libtbbbind.so.3.17',
    'lib/libtbbbind_2_0.so.3',
    'lib/libtbbbind_2_0.so.3.17',
    'lib/libtbbbind_2_5.so.3',
    'lib/libtbbbind_2_5.so.3.17',
  )
  oidn_link_args = ['-Wl,-rpath,$ORIGIN']
elif sys == 'darwin' and cpu == 'x86_64'
  oidn_runtime_files = files(
    'lib/libOpenImageDenoise.dylib',
    'lib/libOpenImageDenoise.2.dylib',
    'lib/libOpenImageDenoise.2.4.1.dylib',
    'lib/libOpenImageDenoise_core.2.4.1.dylib',
    'lib/libOpenImageDenoise_device_cpu.2.4.1.dylib',
    'lib/libtbb.dylib',
    'lib/libtbb.12.dylib',
    'lib/libtbb.12.17.dylib',
  )
  oidn_link_args = ['-Wl,-rpath,@loader_path']
elif sys == 'darwin' and cpu == 'aarch64'
  oidn_runtime_files = files(
    'lib/libOpenImageDenoise.dylib',
    'lib/libOpenImageDenoise.2.dylib',
    'lib/libOpenImageDenoise.2.4.1.dylib',
    'lib/libOpenImageDenoise_core.2.4.1.dylib',
    'lib/libOpenImageDenoise_device_cpu.2.4.1.dylib',
    'lib/libOpenImageDenoise_device_metal.2.4.1.dylib',
    'lib/libtbb.dylib',
    'lib/libtbb.12.dylib',
    'lib/libtbb.12.17.dylib',
  )
  oidn_link_args = ['-Wl,-rpath,@loader_path']
else
  error('No bundled Open Image Denoise package is configured for ' + sys + ' ' + cpu)
endif

oidn_dep = declare_dependency(
  include_directories: oidn_include,
  dependencies: [oidn_lib],
  link_args: oidn_link_args,
)

oidn_version = meson.project_version()
