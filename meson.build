project('VKRT', 'c', default_options: ['c_std=c17'])

cc = meson.get_compiler('c')

glfw3_path = meson.current_source_dir() + '/external/glfw3/lib'
glfw3_lib = cc.find_library('glfw3', dirs: glfw3_path, static: true)

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

incs = [
    include_directories('external/glfw3/include')
]

deps = [
    dependency('vulkan'),
    glfw3_lib
]

executable(
    'vkrt',
    sources,
    dependencies: deps,
    include_directories: incs,
)
