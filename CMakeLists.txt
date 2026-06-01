cmake_minimum_required(VERSION 3.17)

project(gdextension_cpp_example_index LANGUAGES CXX)

file(GLOB_RECURSE GDEXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_library(gdexample_index STATIC EXCLUDE_FROM_ALL ${GDEXAMPLE_SOURCES}
        src/core/rt_math.h
        src/core/rt_random.h
        src/core/rt_color.h
        src/core/rt_light.h
        src/core/rt_light.cpp
        src/scene/rt_scene.h
        src/accel/accel_interface.h)

target_include_directories(gdexample_index PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/gen/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/gdextension"
)

target_compile_features(gdexample_index PRIVATE cxx_std_17)
set_target_properties(gdexample_index PROPERTIES
    CXX_EXTENSIONS OFF
)
