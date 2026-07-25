cmake_minimum_required(VERSION 3.20)

project(object_view LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.7
)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)

find_package(OpenGL REQUIRED)

FetchContent_MakeAvailable(cpp_httplib)
FetchContent_MakeAvailable(glfw)

add_executable(object-view
  backend/src/main.cpp
  backend/src/Model.cpp
  backend/src/Scene.cpp
)

target_include_directories(object-view PRIVATE backend/include)
target_link_libraries(object-view PRIVATE httplib::httplib)

add_executable(object-view-native
  native/src/OpenGLViewer.cpp
  backend/src/Model.cpp
  backend/src/Scene.cpp
)

target_include_directories(object-view-native PRIVATE backend/include)
target_link_libraries(object-view-native PRIVATE glfw OpenGL::GL)

if (WIN32)
  target_link_libraries(object-view-native PRIVATE gdi32 gdiplus)
endif()

if (MSVC)
  target_compile_options(object-view PRIVATE /W4)
  target_compile_options(object-view-native PRIVATE /W4)
else()
  target_compile_options(object-view PRIVATE -Wall -Wextra -pedantic)
  target_compile_options(object-view-native PRIVATE -Wall -Wextra -pedantic)
endif()
