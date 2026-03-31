shader_common_spirv_capabilities = [
  '-capability', 'spvShaderNonUniformEXT',
  '-capability', 'spvMinLod',
  '-capability', 'spvSparseResidency',
  '-capability', 'spvFragmentFullyCoveredEXT',
  '-capability', 'SPV_KHR_non_semantic_info',
  '-capability', 'SPV_GOOGLE_user_type',
]

main_rt_shader_programs = [
  ['rgen',                  'entry/path/raygen_rgb.slang',              'rgen.spv',                     'raygeneration', ['rt'],        'shaderRgen'                    ],
  ['rgenSpectralSingle',    'entry/path/raygen_spectral_single.slang',  'rgenSpectralSingle.spv',       'raygeneration', ['rt'],        'shaderRgenSpectralSingle'      ],
  ['rgenSpectralHero',      'entry/path/raygen_spectral_hero.slang',    'rgenSpectralHero.spv',         'raygeneration', ['rt'],        'shaderRgenSpectralHero'        ],
  ['rchit',                 'entry/path/path_closest_hit.slang',        'rchit.spv',                    'closesthit',    ['rt'],        'shaderRchit'                   ],
  ['rmiss',                 'entry/path/path_miss.slang',               'rmiss.spv',                    'miss',          ['rt'],        'shaderRmiss'                   ],
  ['shadowRchit',           'entry/path/shadow_closest_hit.slang',      'shadowRchit.spv',              'closesthit',    ['rt'],        'shaderShadowRchit'             ],
  ['rahit',                 'entry/path/path_any_hit.slang',            'rahit.spv',                    'anyhit',        ['rt'],        'shaderRahit'                   ],
  ['shadowRahit',           'entry/path/shadow_any_hit.slang',          'shadowRahit.spv',              'anyhit',        ['rt'],        'shaderShadowRahit'             ],
  ['shadowMiss',            'entry/path/shadow_miss.slang',             'shadowMiss.spv',               'miss',          ['rt'],        'shaderShadowMiss'              ],
  ['rgenSer',               'entry/path/raygen_rgb.slang',              'rgenSer.spv',                  'raygeneration', ['rt', 'ser'], 'shaderRgenSer'                 ],
  ['rgenSpectralSingleSer', 'entry/path/raygen_spectral_single.slang',  'rgenSpectralSingleSer.spv',    'raygeneration', ['rt', 'ser'], 'shaderRgenSpectralSingleSer'   ],
  ['rgenSpectralHeroSer',   'entry/path/raygen_spectral_hero.slang',    'rgenSpectralHeroSer.spv',      'raygeneration', ['rt', 'ser'], 'shaderRgenSpectralHeroSer'     ],
  ['rchitSer',              'entry/path/path_closest_hit.slang',        'rchitSer.spv',                 'closesthit',    ['rt', 'ser'], 'shaderRchitSer'                ],
  ['rmissSer',              'entry/path/path_miss.slang',               'rmissSer.spv',                 'miss',          ['rt', 'ser'], 'shaderRmissSer'                ],
  ['shadowRchitSer',        'entry/path/shadow_closest_hit.slang',      'shadowRchitSer.spv',           'closesthit',    ['rt', 'ser'], 'shaderShadowRchitSer'          ],
  ['shadowMissSer',         'entry/path/shadow_miss.slang',             'shadowMissSer.spv',            'miss',          ['rt', 'ser'], 'shaderShadowMissSer'           ],
]

selection_rt_shader_programs = [
  ['selectRgen',  'entry/select/selection_raygen.slang',       'selectRgen.spv',   'raygeneration', ['rt'], 'shaderSelectRgen'  ],
  ['selectRchit', 'entry/select/selection_closest_hit.slang',  'selectRchit.spv',  'closesthit',    ['rt'], 'shaderSelectRchit' ],
  ['selectRahit', 'entry/select/selection_any_hit.slang',      'selectRahit.spv',  'anyhit',        ['rt'], 'shaderSelectRahit' ],
  ['selectRmiss', 'entry/select/selection_miss.slang',         'selectRmiss.spv',  'miss',          ['rt'], 'shaderSelectRmiss' ],
]

post_shader_programs = [
  ['comp', 'entry/post/outline.slang', 'select.spv', 'compute', [], 'shaderComp'],
]

shader_programs = main_rt_shader_programs + selection_rt_shader_programs + post_shader_programs

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
