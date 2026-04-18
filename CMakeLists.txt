cmake_minimum_required(VERSION 3.14)
project(tornado_sim VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# GLFW
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.3.8
)

# glad
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG        v0.1.36
)

# glm
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        0.9.9.8
)

FetchContent_MakeAvailable(glfw glad glm)

find_package(OpenGL REQUIRED)

add_executable(tornado src/main.cpp src/tinygltf_impl.cpp)

target_include_directories(tornado PRIVATE ${glad_SOURCE_DIR}/include)
target_include_directories(tornado PRIVATE ${CMAKE_SOURCE_DIR}/vendor)
# glm headers are header-only; FetchContent provides target glm::glm

target_link_libraries(tornado PRIVATE glfw glad OpenGL::GL glm::glm)

target_compile_definitions(tornado PRIVATE "GLFW_INCLUDE_NONE")

# Copy shaders to build dir so binary can load them at runtime
add_custom_command(TARGET tornado POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:tornado>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/shaders/vertex.glsl
    $<TARGET_FILE_DIR:tornado>/shaders/vertex.glsl
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/shaders/fragment.glsl
    $<TARGET_FILE_DIR:tornado>/shaders/fragment.glsl
)

# Copy assets (models, textures) to binary dir
add_custom_command(TARGET tornado POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:tornado>/assets
)

message(STATUS "Configured Tornado Sim (OpenGL + GLFW + GLAD). Build with cmake and make or ninja.")
