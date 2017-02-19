cmake_minimum_required (VERSION 3.1)
project (father)

add_subdirectory(vendor/libtwittercpp)
add_subdirectory(vendor/verbly)
add_subdirectory(vendor/yaml-cpp EXCLUDE_FROM_ALL)

include_directories(vendor/libtwittercpp/src vendor/yaml-cpp/include vendor/verbly/lib)
add_executable(father father.cpp)
set_property(TARGET father PROPERTY CXX_STANDARD 11)
set_property(TARGET father PROPERTY CXX_STANDARD_REQUIRED ON)
target_link_libraries(father verbly yaml-cpp twitter++)

