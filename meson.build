project('VKRT', 'c', default_options : ['cpp_std=c23'])

cc = meson.get_compiler('c')

glfw3_path = meson.current_source_dir() + '/src/external/glfw3/lib'
glfw3_lib = cc.find_library('glfw3', dirs: glfw3_path)

sources = [
    'src/cleanup.c',
    'src/device.c',
    'src/instance.c',
    'src/main.c',
    'src/surface.c',
    'src/validation.c',
    'src/vulkan.c',
    'src/window.c'
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
