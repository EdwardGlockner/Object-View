cmake_minimum_required(VERSION 3.20)

project(object_view LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

option(OBJECT_VIEW_FETCH_WXWIDGETS "Fetch wxWidgets when it is not already installed." ON)

FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.7
)

find_package(OpenGL REQUIRED)

set(OBJECT_VIEW_WX_TARGETS "")
set(OBJECT_VIEW_WX_USE_FILE "")

find_package(wxWidgets QUIET COMPONENTS core base gl)

if (wxWidgets_FOUND)
  set(OBJECT_VIEW_WX_USE_FILE ${wxWidgets_USE_FILE})
elseif (OBJECT_VIEW_FETCH_WXWIDGETS)
  set(wxBUILD_SHARED OFF CACHE BOOL "Build wxWidgets as static libraries." FORCE)
  set(wxBUILD_SAMPLES OFF CACHE BOOL "Skip wxWidgets samples." FORCE)
  set(wxBUILD_TESTS OFF CACHE BOOL "Skip wxWidgets tests." FORCE)
  set(wxBUILD_DEMOS OFF CACHE BOOL "Skip wxWidgets demos." FORCE)
  set(wxBUILD_BENCHMARKS OFF CACHE BOOL "Skip wxWidgets benchmarks." FORCE)
  set(wxUSE_OPENGL ON CACHE BOOL "Enable OpenGL support in wxWidgets." FORCE)

  FetchContent_Declare(
    wxwidgets
    GIT_REPOSITORY https://github.com/wxWidgets/wxWidgets.git
    GIT_TAG v3.2.7
  )

  FetchContent_MakeAvailable(wxwidgets)
  set(OBJECT_VIEW_WX_TARGETS wx::core wx::base wx::gl)
else()
  message(FATAL_ERROR "wxWidgets is required for object-view-native.")
endif()

FetchContent_MakeAvailable(cpp_httplib)

add_executable(object-view
  backend/src/main.cpp
  backend/src/Model.cpp
)

target_include_directories(object-view PRIVATE backend/include)
target_link_libraries(object-view PRIVATE httplib::httplib)

if (OBJECT_VIEW_WX_USE_FILE)
  include(${OBJECT_VIEW_WX_USE_FILE})
endif()

add_executable(object-view-native
  native/src/MyApp.cpp
  native/src/GLCanvas.cpp
  backend/src/Model.cpp
)

target_include_directories(object-view-native PRIVATE backend/include native/include)
if (OBJECT_VIEW_WX_TARGETS)
  target_link_libraries(object-view-native PRIVATE ${OBJECT_VIEW_WX_TARGETS} OpenGL::GL)
else()
  target_link_libraries(object-view-native PRIVATE ${wxWidgets_LIBRARIES} OpenGL::GL)
endif()

if (WIN32)
  target_link_libraries(object-view-native PRIVATE gdi32 gdiplus opengl32 winmm)
endif()

if (MSVC)
  target_compile_options(object-view PRIVATE /W4)
  target_compile_options(object-view-native PRIVATE /W4)
else()
  target_compile_options(object-view PRIVATE -Wall -Wextra -pedantic)
  target_compile_options(object-view-native PRIVATE -Wall -Wextra -pedantic)
endif()
