shader_programs = [
  ['rgen', 'rt/main/main.rgen', 'rgen.spv'],
  ['rchit', 'rt/main/main.rchit', 'rchit.spv'],
  ['rmiss', 'rt/main/main.rmiss', 'rmiss.spv'],
  ['comp', 'compute/select.comp', 'select.spv'],
  ['select_rgen', 'rt/selection/select.rgen', 'select_rgen.spv'],
  ['select_rchit', 'rt/selection/select.rchit', 'select_rchit.spv'],
  ['select_rmiss', 'rt/selection/select.rmiss', 'select_rmiss.spv'],
  ['select_rahit', 'rt/selection/select.rahit', 'select_rahit.spv'],
]

shader_targets = []
foreach shader_program : shader_programs
  stage_name = shader_program[0]
  shader_file = shader_program[1]
  output_file = shader_program[2]
  shader_target = custom_target(
    'compile_shader_' + stage_name,
    input: shader_file,
    output: output_file,
    depfile: output_file + '.d',
    command: [
      glslc,
      '@INPUT@',
      '--target-env=vulkan1.4',
      '-O',
      '-MD',
      '-MF', '@DEPFILE@',
      '-MT', '@OUTPUT@',
      '-I', meson.current_source_dir(),
      '-I', join_paths(meson.project_source_root(), 'shared'),
      '-o', '@OUTPUT@',
    ],
    build_by_default: true,
  )
  shader_targets += shader_target
endforeach
