project('VKRT', 'c')

cc = meson.get_compiler('c')

glfw3_path = meson.current_source_dir() + '/src/external/glfw3/lib'
glfw3_lib = cc.find_library('glfw3', dirs: glfw3_path)

sources = [
    'src/app.c',
    'src/command.c',
    'src/device.c',
    'src/instance.c',
    'src/main.c',
    'src/pipeline.c',
    'src/surface.c',
    'src/swapchain.c',
    'src/validation.c'
]

incs = [
    include_directories('src/external/glfw3/include')
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
