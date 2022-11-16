cmake_minimum_required (VERSION 3.1)
project (father)

find_package(PkgConfig)
pkg_check_modules(yaml-cpp yaml-cpp REQUIRED)
pkg_check_modules(mastodonpp mastodonpp REQUIRED)

add_subdirectory(vendor/verbly)

include_directories(
  ${mastodonpp_INCLUDE_DIRS}
  vendor/verbly/lib
  ${yaml-cpp_INCLUDE_DIRS}
  vendor/hkutil
  vendor/json)

link_directories(
  ${mastodonpp_LIBRARY_DIRS}
  ${yaml-cpp_LIBRARY_DIRS})

add_executable(father father.cpp timeline.cpp)
set_property(TARGET father PROPERTY CXX_STANDARD 17)
set_property(TARGET father PROPERTY CXX_STANDARD_REQUIRED ON)
target_link_libraries(father verbly ${mastodonpp_LIBRARIES} ${yaml-cpp_LIBRARIES})
