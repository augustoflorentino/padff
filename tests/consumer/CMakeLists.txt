cmake_minimum_required(VERSION 3.16)
project(padff_consumer LANGUAGES C)

find_package(padff REQUIRED)

add_executable(consumer ${CMAKE_CURRENT_SOURCE_DIR}/../../examples/basic.c)
target_link_libraries(consumer PRIVATE padff::padff)
