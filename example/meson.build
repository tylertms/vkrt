project('vkrt-example', ['c', 'cpp'],
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=2',
  ],
)

vkrt_dep = dependency('vkrt', fallback: ['vkrt', 'vkrt_dep'])
glslc = find_program('glslc')

shader_inputs = [
    'src/shaders/main.rchit',
    'src/shaders/main.rgen',
    'src/shaders/main.rmiss',
]

shader_targets = []
foreach shader_in : shader_inputs
    basename = shader_in.split('/')[-1]
    shader_targets += custom_target(
        'compile_' + basename,
        input: shader_in,
        output: basename + '.spv',
        command: [glslc, '@INPUT@', '--target-env=vulkan1.4', '-O', '-o', '@OUTPUT@'],
        build_by_default: true,
    )
endforeach

sources = [
    'src/main.c',
]

executable(
    'vkrt-example',
    sources,
    dependencies: [vkrt_dep],
    link_language: 'cpp'
)