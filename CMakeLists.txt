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
        src/accel/accel_interface.h
        src/util/logger.h
        src/accel/brute_force_accel.h
        src/render/integrators.cpp
        godot-cpp/test/src/register_types.cpp)

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


include(CTest)
if(BUILD_TESTING)
    add_executable(raytrace_postprocess_tests
        tests/render/color_postprocess_tests.cpp
        src/render/color_postprocess.cpp
    )
    target_include_directories(raytrace_postprocess_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_features(raytrace_postprocess_tests PRIVATE cxx_std_17)
    if(MSVC)
        # 测试目标包含项目内 UTF-8 中文注释的头文件，显式避免使用 ACP 解码。
        target_compile_options(raytrace_postprocess_tests PRIVATE /utf-8)
    endif()
    set_target_properties(raytrace_postprocess_tests PROPERTIES
        CXX_EXTENSIONS OFF
    )
    add_test(NAME raytrace_postprocess_tests COMMAND raytrace_postprocess_tests)
endif()
