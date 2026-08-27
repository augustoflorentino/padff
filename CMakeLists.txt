cmake_minimum_required(VERSION 3.16)
project(padff VERSION 2.4.0 DESCRIPTION "Force feedback mixer: game rumble effects to two motor intensities" LANGUAGES C)

option(PADFF_BUILD_TESTS "Build tests" OFF)
option(PADFF_BUILD_EXAMPLES "Build examples" OFF)
option(PADFF_BUILD_FUZZ "Build libFuzzer targets" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(padff src/padff.c)
add_library(padff::padff ALIAS padff)
target_include_directories(padff PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(padff PROPERTIES
    C_STANDARD 99
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

set(PC_CFLAGS "")
if(BUILD_SHARED_LIBS)
    target_compile_definitions(padff PRIVATE PFF_BUILD_SHARED INTERFACE PFF_SHARED)
    set(PC_CFLAGS "-DPFF_SHARED")
endif()

install(TARGETS padff EXPORT padffTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT padffTargets
    NAMESPACE padff::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/padff)

configure_package_config_file(cmake/padffConfig.cmake.in
    ${PROJECT_BINARY_DIR}/padffConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/padff)
write_basic_package_version_file(${PROJECT_BINARY_DIR}/padffConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${PROJECT_BINARY_DIR}/padffConfig.cmake
    ${PROJECT_BINARY_DIR}/padffConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/padff)

configure_file(padff.pc.in ${PROJECT_BINARY_DIR}/padff.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/padff.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(PADFF_BUILD_TESTS)
    enable_testing()
    add_executable(test_padff tests/test_padff.c)
    target_link_libraries(test_padff PRIVATE padff)
    add_test(NAME padff COMMAND test_padff)
    add_executable(padff_trace_run tests/trace_run.c)
    target_link_libraries(padff_trace_run PRIVATE padff)
    file(GLOB padff_traces ${PROJECT_SOURCE_DIR}/tests/traces/*.trace)
    foreach(trace ${padff_traces})
        get_filename_component(name ${trace} NAME_WE)
        string(REGEX REPLACE "[.]trace$" ".expected" expected ${trace})
        add_test(NAME trace_${name} COMMAND padff_trace_run ${trace} ${expected})
    endforeach()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME oracle_random
            COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tests/oracle.py random
                $<TARGET_FILE:padff_trace_run> 300)
    endif()
    add_library(padff_header_check OBJECT tests/header_check.c)
    target_include_directories(padff_header_check PRIVATE include)
endif()

if(PADFF_BUILD_EXAMPLES)
    add_executable(padff_basic examples/basic.c)
    target_link_libraries(padff_basic PRIVATE padff)
    add_executable(padff_ff_replay examples/ff_replay.c)
    target_link_libraries(padff_ff_replay PRIVATE padff)
endif()

if(PADFF_BUILD_FUZZ)
    add_executable(fuzz_commands fuzz/fuzz_commands.c)
    target_link_libraries(fuzz_commands PRIVATE padff)
    target_link_options(fuzz_commands PRIVATE -fsanitize=fuzzer)
endif()
