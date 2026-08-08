# CMake generated Testfile for 
# Source directory: D:/gdextension_cpp_example
# Build directory: D:/gdextension_cpp_example/build/req001-tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(raytrace_postprocess_tests "D:/gdextension_cpp_example/build/req001-tests/Debug/raytrace_postprocess_tests.exe")
  set_tests_properties(raytrace_postprocess_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/gdextension_cpp_example/CMakeLists.txt;53;add_test;D:/gdextension_cpp_example/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(raytrace_postprocess_tests "D:/gdextension_cpp_example/build/req001-tests/Release/raytrace_postprocess_tests.exe")
  set_tests_properties(raytrace_postprocess_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/gdextension_cpp_example/CMakeLists.txt;53;add_test;D:/gdextension_cpp_example/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(raytrace_postprocess_tests "D:/gdextension_cpp_example/build/req001-tests/MinSizeRel/raytrace_postprocess_tests.exe")
  set_tests_properties(raytrace_postprocess_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/gdextension_cpp_example/CMakeLists.txt;53;add_test;D:/gdextension_cpp_example/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(raytrace_postprocess_tests "D:/gdextension_cpp_example/build/req001-tests/RelWithDebInfo/raytrace_postprocess_tests.exe")
  set_tests_properties(raytrace_postprocess_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/gdextension_cpp_example/CMakeLists.txt;53;add_test;D:/gdextension_cpp_example/CMakeLists.txt;0;")
else()
  add_test(raytrace_postprocess_tests NOT_AVAILABLE)
endif()
