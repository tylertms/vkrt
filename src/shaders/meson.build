shader_common_spirv_capabilities = [
  '-capability', 'spvShaderNonUniformEXT',
  '-capability', 'spvMinLod',
  '-capability', 'spvSparseResidency',
  '-capability', 'spvFragmentFullyCoveredEXT',
  '-capability', 'SPV_KHR_non_semantic_info',
  '-capability', 'SPV_GOOGLE_user_type',
]

shader_programs = [
  ['rgen',                  'path/raygen.slang',                    'rgen.spv',                     'raygeneration', ['rt'],        'shaderRgen'                    ],
  ['rgenSpectralSingle',    'path/raygen_spectral_single.slang',    'rgenSpectralSingle.spv',       'raygeneration', ['rt'],        'shaderRgenSpectralSingle'      ],
  ['rgenSpectralHero',      'path/raygen_spectral_hero.slang',   '  rgenSpectralHero.spv',          'raygeneration', ['rt'],        'shaderRgenSpectralHero'        ],
  ['rchit',                 'path/hit.slang',                       'rchit.spv',                    'closesthit',    ['rt'],        'shaderRchit'                   ],
  ['rmiss',                 'path/miss.slang',                      'rmiss.spv',                    'miss',          ['rt'],        'shaderRmiss'                   ],
  ['shadowRchit',           'path/shadow_hit.slang',                'shadowRchit.spv',              'closesthit',    ['rt'],        'shaderShadowRchit'             ],
  ['rahit',                 'path/any.slang',                       'rahit.spv',                    'anyhit',        ['rt'],        'shaderRahit'                   ],
  ['shadowRahit',           'path/shadow_any.slang',                'shadowRahit.spv',              'anyhit',        ['rt'],        'shaderShadowRahit'             ],
  ['shadowMiss',            'path/shadow_miss.slang',               'shadowMiss.spv',               'miss',          ['rt'],        'shaderShadowMiss'              ],
  ['rgenSer',               'path/raygen.slang',                    'rgenSer.spv',                  'raygeneration', ['rt', 'ser'], 'shaderRgenSer'                 ],
  ['rgenSpectralSingleSer', 'path/raygen_spectral_single.slang',    'rgenSpectralSingleSer.spv',    'raygeneration', ['rt', 'ser'], 'shaderRgenSpectralSingleSer'   ],
  ['rgenSpectralHeroSer',   'path/raygen_spectral_hero.slang',      'rgenSpectralHeroSer.spv',      'raygeneration', ['rt', 'ser'], 'shaderRgenSpectralHeroSer'     ],
  ['rchitSer',              'path/hit.slang',                       'rchitSer.spv',                 'closesthit',    ['rt', 'ser'], 'shaderRchitSer'                ],
  ['rmissSer',              'path/miss.slang',                      'rmissSer.spv',                 'miss',          ['rt', 'ser'], 'shaderRmissSer'                ],
  ['shadowRchitSer',        'path/shadow_hit.slang',                'shadowRchitSer.spv',           'closesthit',    ['rt', 'ser'], 'shaderShadowRchitSer'          ],
  ['shadowMissSer',         'path/shadow_miss.slang',               'shadowMissSer.spv',            'miss',          ['rt', 'ser'], 'shaderShadowMissSer'           ],
]

shader_programs += [
  ['comp',        'post/outline.slang',   'select.spv',      'compute',       [],     'shaderComp'       ],
  ['selectRgen',  'select/raygen.slang',  'selectRgen.spv',  'raygeneration', ['rt'], 'shaderSelectRgen' ],
  ['selectRchit', 'select/hit.slang',     'selectRchit.spv', 'closesthit',    ['rt'], 'shaderSelectRchit'],
  ['selectRahit', 'select/any.slang',     'selectRahit.spv', 'anyhit',        ['rt'], 'shaderSelectRahit'],
  ['selectRmiss', 'select/miss.slang',    'selectRmiss.spv', 'miss',          ['rt'], 'shaderSelectRmiss'],
]

embedded_shader_sources = []
foreach shader_program : shader_programs
  stage_name = shader_program[0]
  shader_file = shader_program[1]
  output_file = shader_program[2]
  shader_stage = shader_program[3]
  shader_features = shader_program[4]
  shader_symbol_name = shader_program[5]
  shader_capabilities = shader_common_spirv_capabilities
  shader_extra_args = []

  if 'rt' in shader_features
    shader_capabilities += ['-capability', 'spvRayTracingKHR']
  endif

  if 'ser' in shader_features
    shader_capabilities += [
      '-capability', 'SPV_EXT_shader_invocation_reorder',
      '-capability', 'spvShaderInvocationReorderEXT'
    ]
    shader_extra_args += [
      '-DVKRT_SER_ENABLED',
      '-Wno-41012',
    ]
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
    command: [embed_data, '--format', 'u32le', '@INPUT@', '@OUTPUT@', shader_symbol_name],
  )
  embedded_shader_sources += embed_target
endforeach
