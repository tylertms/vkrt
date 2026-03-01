shader_programs = [
  ['rgen', 'main.rgen', 'rgen.spv'],
  ['rchit', 'main.rchit', 'rchit.spv'],
  ['rmiss', 'main.rmiss', 'rmiss.spv'],
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
