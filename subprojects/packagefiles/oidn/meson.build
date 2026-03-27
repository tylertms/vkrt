project(
  'oidn',
  'c',
  version: '2.4.1',
)

cc = meson.get_compiler('c')
sys = host_machine.system()
cpu = host_machine.cpu_family()
python = find_program('python3', 'python', required: true)

oidn_include = include_directories('include', is_system: true)
oidn_lib = cc.find_library('OpenImageDenoise', dirs: [meson.current_source_dir() / 'lib'], required: true)
oidn_runtime_files = []
oidn_link_args = []
oidn_runtime_dir = ''
oidn_runtime_kind = ''

if sys == 'windows' and cpu == 'x86_64'
  oidn_runtime_dir = 'bin'
  oidn_runtime_kind = 'windows'
elif sys == 'linux' and cpu == 'x86_64'
  oidn_runtime_dir = 'lib'
  oidn_runtime_kind = 'linux'
  oidn_link_args = ['-Wl,-rpath,$ORIGIN']
elif sys == 'darwin' and cpu == 'x86_64'
  oidn_runtime_dir = 'lib'
  oidn_runtime_kind = 'darwin'
  oidn_link_args = ['-Wl,-rpath,@loader_path']
elif sys == 'darwin' and cpu == 'aarch64'
  oidn_runtime_dir = 'lib'
  oidn_runtime_kind = 'darwin'
  oidn_link_args = ['-Wl,-rpath,@loader_path']
else
  error('No bundled Open Image Denoise package is configured for ' + sys + ' ' + cpu)
endif

oidn_runtime_names = run_command(
  python,
  '-c',
  '''
from pathlib import Path
import sys

root = Path(sys.argv[1])
kind = sys.argv[2]

if kind == 'windows':
    keep = lambda name: name.endswith('.dll')
elif kind == 'darwin':
    keep = lambda name: name.endswith('.dylib')
else:
    keep = lambda name: '.so' in name

names = sorted(path.name for path in root.iterdir() if path.is_file() and keep(path.name))
print(' '.join(names))
''',
  meson.current_source_dir() / oidn_runtime_dir,
  oidn_runtime_kind,
  check: true,
).stdout().strip().split()

foreach oidn_runtime_name : oidn_runtime_names
  if oidn_runtime_name != ''
    oidn_runtime_files += files(oidn_runtime_dir + '/' + oidn_runtime_name)
  endif
endforeach

oidn_dep = declare_dependency(
  include_directories: oidn_include,
  dependencies: [oidn_lib],
  link_args: oidn_link_args,
)

oidn_version = meson.project_version()
