@echo OFF
glslc .\src\shaders\main.rchit --target-env=vulkan1.4 -O -o .\shaders\rchit.spv
glslc .\src\shaders\main.rmiss --target-env=vulkan1.4 -O -o .\shaders\rmiss.spv
glslc .\src\shaders\main.rgen --target-env=vulkan1.4 -O -o .\shaders\rgen.spv