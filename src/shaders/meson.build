embed_spv = find_program(meson.project_source_root() / 'scripts' / 'embed_spv.py')

shader_programs = [
  ['rgen', 'rt/main/rgen.slang', 'rgen.spv', 'raygeneration', 'rt'],
  ['rchit', 'rt/main/rchit.slang', 'rchit.spv', 'closesthit', 'rt'],
  ['rmiss', 'rt/main/rmiss.slang', 'rmiss.spv', 'miss', 'rt'],
]
if selection_enabled
  shader_programs += [
    ['comp', 'compute/outline.slang', 'select.spv', 'compute', 'compute'],
    ['select_rgen', 'rt/selection/rgen.slang', 'select_rgen.spv', 'raygeneration', 'rt'],
    ['select_rchit', 'rt/selection/rchit.slang', 'select_rchit.spv', 'closesthit', 'rt'],
    ['select_rmiss', 'rt/selection/rmiss.slang', 'select_rmiss.spv', 'miss', 'rt'],
  ]
endif

embedded_shader_sources = []
foreach shader_program : shader_programs
  stage_name = shader_program[0]
  shader_file = shader_program[1]
  output_file = shader_program[2]
  shader_stage = shader_program[3]
  shader_kind = shader_program[4]
  shader_capabilities = []

  if shader_kind == 'rt'
    shader_capabilities += ['-capability', 'spvRayTracingKHR']
  endif

  shader_target = custom_target(
    'compile_shader_' + stage_name,
    input: shader_file,
    output: output_file,
    depfile: output_file + '.d',
    command: [
      slangc,
      '@INPUT@',
      '-target', 'spirv',
      '-profile', 'glsl_460',
      '-entry', 'main',
      '-stage', shader_stage,
      '-matrix-layout-column-major',
      '-fvk-use-gl-layout',
      slang_compile_args,
      shader_capabilities,
      '-depfile', '@DEPFILE@',
      '-I', meson.current_source_dir(),
      '-DVKRT_SHADER',
      '-o', '@OUTPUT@',
    ],
    build_by_default: true,
  )

  embed_target = custom_target(
    'embed_shader_' + stage_name,
    input: shader_target,
    output: 'shader_' + stage_name + '.c',
    command: [embed_spv, '@INPUT@', '@OUTPUT@', 'shader_' + stage_name],
  )
  embedded_shader_sources += embed_target
endforeach
