embed_spv = find_program(meson.project_source_root() / 'scripts' / 'embed_spv.py')

shader_programs = [
  ['rgen',          'rt/main/rgen.slang',   'rgen.spv',          'raygeneration', ['rt'],        'shaderRgen'         ],
  ['rchit',         'rt/main/rchit.slang',  'rchit.spv',         'closesthit',    ['rt'],        'shaderRchit'        ],
  ['rmiss',         'rt/main/rmiss.slang',  'rmiss.spv',         'miss',          ['rt'],        'shaderRmiss'        ],
  ['shadowMiss',    'rt/main/shadow.slang', 'shadowMiss.spv',    'miss',          ['rt'],        'shaderShadowMiss'   ],
  ['rgenSer',       'rt/main/rgen.slang',   'rgenSer.spv',       'raygeneration', ['rt', 'ser'], 'shaderRgenSer'      ],
  ['rchitSer',      'rt/main/rchit.slang',  'rchitSer.spv',      'closesthit',    ['rt', 'ser'], 'shaderRchitSer'     ],
  ['rmissSer',      'rt/main/rmiss.slang',  'rmissSer.spv',      'miss',          ['rt', 'ser'], 'shaderRmissSer'     ],
  ['shadowMissSer', 'rt/main/shadow.slang', 'shadowMissSer.spv', 'miss',          ['rt', 'ser'], 'shaderShadowMissSer'],
]

if selection_enabled
  shader_programs += [
    ['comp',        'compute/outline.slang',    'select.spv',      'compute',       [],     'shaderComp'       ],
    ['selectRgen',  'rt/selection/rgen.slang',  'selectRgen.spv',  'raygeneration', ['rt'], 'shaderSelectRgen' ],
    ['selectRchit', 'rt/selection/rchit.slang', 'selectRchit.spv', 'closesthit',    ['rt'], 'shaderSelectRchit'],
    ['selectRmiss', 'rt/selection/rmiss.slang', 'selectRmiss.spv', 'miss',          ['rt'], 'shaderSelectRmiss'],
  ]
endif

embedded_shader_sources = []
foreach shader_program : shader_programs
  stage_name = shader_program[0]
  shader_file = shader_program[1]
  output_file = shader_program[2]
  shader_stage = shader_program[3]
  shader_features = shader_program[4]
  shader_symbol_name = shader_program[5]
  shader_capabilities = []
  shader_extra_args = []

  if 'rt' in shader_features
    shader_capabilities += ['-capability', 'spvRayTracingKHR']
  endif

  if 'ser' in shader_features
    shader_capabilities += ['-capability', 'spvShaderInvocationReorderNV']
    shader_extra_args += ['-DVKRT_SER_ENABLED']
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
      shader_extra_args,
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
    command: [embed_spv, '@INPUT@', '@OUTPUT@', shader_symbol_name],
  )
  embedded_shader_sources += embed_target
endforeach
