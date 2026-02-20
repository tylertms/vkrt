shader_sources = [
  'main.rchit',
  'main.rgen',
  'main.rmiss',
]

shader_targets = []
foreach shader_file : shader_sources
  basename = shader_file.split('.')[-1]
  shader_target = custom_target(
    'compile_' + basename,
    input: shader_file,
    output: basename + '.spv',
    depfile: basename + '.d',
    command: [
      glslc,
      '@INPUT@',
      '--target-env=vulkan1.4',
      '-O',
      '-MD',
      '-MF', '@DEPFILE@',
      '-MT', '@OUTPUT@',
      '-I', meson.current_source_dir(),
      '-o', '@OUTPUT@',
    ],
    build_by_default: true,
  )
  shader_targets += shader_target
endforeach
