project('VKRT', 'cpp')

cpp = meson.get_compiler('cpp')

glfw3_path = meson.current_source_dir() + '/src/external/glfw3/lib'
glfw3_lib = cpp.find_library('libglfw3', dirs: glfw3_path)

sources = [
    'src/main.cpp',
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
