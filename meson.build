project('VKRT', 'c', default_options: ['c_std=c17', 'warning_level=3'])

cxx = meson.get_compiler('c')

glfw3_path = meson.current_source_dir() / 'external' / 'glfw3' / 'lib'
glfw3_lib = cxx.find_library('glfw3', dirs: glfw3_path, static: true)

incs = include_directories('external/glfw3/include')
deps = [ dependency('vulkan'), glfw3_lib ]

glslc = find_program('glslc')

sources = [
    'src/app.c',
    'src/buffer.c',
    'src/command.c',
    'src/descriptor.c',
    'src/device.c',
    'src/instance.c',
    'src/main.c',
    'src/pipeline.c',
    'src/structure.c',
    'src/surface.c',
    'src/swapchain.c',
    'src/validation.c'
]

shader_inputs = [
    'src/shaders/main.rchit',
    'src/shaders/main.rgen',
    'src/shaders/main.rmiss'
]

shader_targets = []
foreach shader_in : shader_inputs
    basename = shader_in.split('.')[-1]
    shader_target = custom_target(
        'compile_' + basename,
        input: shader_in,
        output: basename + '.spv',
        command: [glslc, '@INPUT@', '--target-env=vulkan1.4', '-O', '-o', '@OUTPUT@'],
        build_by_default: true
    )
    shader_targets += shader_target
endforeach

executable(
    'vkrt',
    sources,
    include_directories: incs,
    dependencies: deps
)