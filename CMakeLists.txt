cmake_minimum_required(VERSION 3.22)
project(camster LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Vulkan REQUIRED)
include(FetchContent)
include(CTest)

if(UNIX AND NOT APPLE AND NOT WIN32)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Disable Wayland backend" FORCE)
  set(GLFW_BUILD_X11 ON CACHE BOOL "Enable X11 backend" FORCE)
endif()

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9b
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

set(CAMSTER_SOURCES
  src/main.cpp
  src/AppSettings.cpp
  src/cam/Cam.cpp
  src/core/StructuredLog.cpp
  src/dfm/Dfm.cpp
  src/drawing/Drawing.cpp
  src/interop/Step.cpp
  src/NutFile.cpp
  src/core/AppLogic.cpp
  src/CameraController.cpp
  src/Gizmo.cpp
  src/History.cpp
  src/StlMesh.cpp
  src/VulkanRenderer.cpp
    src/PrintSettings.cpp
    src/sketch/Extrude.cpp
  src/sketch/ExtrudeTool.cpp
  src/sketch/Sketch.cpp
  src/sketch/SketchTool.cpp
  src/ui/FileBrowser.cpp
  src/ui/UIWindows.cpp
  src/ui/Toolbar.cpp
    src/ui/PrintWindow.cpp
)

add_executable(camster ${CAMSTER_SOURCES})

target_include_directories(camster PRIVATE
  src
)

target_link_libraries(camster PRIVATE
  glfw
  glm
  imgui
  Vulkan::Vulkan
)

target_compile_definitions(camster PRIVATE
  CAMSTER_ENABLE_VALIDATION=$<CONFIG:Debug>
)

find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)
find_program(GLSLANG_VALIDATOR glslangValidator HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)

if(GLSLC)
  set(SHADER_COMPILER ${GLSLC})
  set(SHADER_COMPILE_ARGS)
elseif(GLSLANG_VALIDATOR)
  set(SHADER_COMPILER ${GLSLANG_VALIDATOR})
  set(SHADER_COMPILE_ARGS -V)
else()
  message(FATAL_ERROR "Neither glslc nor glslangValidator was found. Install Vulkan SDK tools.")
endif()

set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(SHADER_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/mesh.vert
  ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/mesh.frag
  ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/line.vert
  ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/line.frag
)

set(COMPILED_SHADERS)
foreach(SHADER_FILE IN LISTS SHADER_SOURCES)
  get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
  set(SPIRV_FILE ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
  add_custom_command(
    OUTPUT ${SPIRV_FILE}
    COMMAND ${SHADER_COMPILER} ${SHADER_COMPILE_ARGS} ${SHADER_FILE} -o ${SPIRV_FILE}
    DEPENDS ${SHADER_FILE}
    COMMENT "Compiling ${SHADER_NAME}"
    VERBATIM
  )
  list(APPEND COMPILED_SHADERS ${SPIRV_FILE})
endforeach()

add_custom_target(shaders ALL DEPENDS ${COMPILED_SHADERS})
add_dependencies(camster shaders)

install(TARGETS camster RUNTIME DESTINATION bin COMPONENT Runtime)
install(FILES ${COMPILED_SHADERS} DESTINATION bin/shaders COMPONENT Runtime)
install(FILES
  assets/sample_cube.stl
  DESTINATION bin/assets
  COMPONENT Runtime
)

target_compile_definitions(camster PRIVATE
  CAMSTER_SHADER_DIR="${SHADER_OUTPUT_DIR}"
)

if(BUILD_TESTING)
  add_executable(camster_regression_suite
    tests/regression_suite.cpp
    src/StlMesh.cpp
    src/sketch/Extrude.cpp
    src/sketch/Sketch.cpp
    src/cam/Cam.cpp
    src/dfm/Dfm.cpp
    src/drawing/Drawing.cpp
    src/interop/Step.cpp
  )
  target_include_directories(camster_regression_suite PRIVATE src)
  target_link_libraries(camster_regression_suite PRIVATE glm)

  add_executable(camster_perf_bench
    tests/perf_bench.cpp
    src/StlMesh.cpp
    src/sketch/Extrude.cpp
    src/sketch/Sketch.cpp
    src/cam/Cam.cpp
  )
  target_include_directories(camster_perf_bench PRIVATE src)
  target_link_libraries(camster_perf_bench PRIVATE glm)

  add_test(NAME regression_suite COMMAND camster_regression_suite)
  add_test(NAME perf_bench COMMAND camster_perf_bench)
endif()
