cmake_minimum_required (VERSION 3.1)
project (father)

find_package(PkgConfig)
pkg_check_modules(yaml-cpp yaml-cpp REQUIRED)

add_subdirectory(vendor/libtwittercpp)
add_subdirectory(vendor/verbly)

include_directories(
  vendor/libtwittercpp/src
  vendor/verbly/lib
  ${yaml-cpp_INCLUDE_DIRS})

add_executable(father father.cpp)
set_property(TARGET father PROPERTY CXX_STANDARD 11)
set_property(TARGET father PROPERTY CXX_STANDARD_REQUIRED ON)
target_link_libraries(father verbly twitter++ ${yaml-cpp_LIBRARIES})
