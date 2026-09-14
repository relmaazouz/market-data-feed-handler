cmake_minimum_required(VERSION 3.20)
project(market-data-feed-handler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ITCH_BUILD_APPS "Build the itch_feed / itch_bench demo binaries" ON)
option(ITCH_BUILD_TESTS "Build and register the unit test binary" ON)
option(ITCH_ENABLE_NATIVE_ARCH "Compile this repo's own targets with -march=native (not portable across machines)" ON)
option(ITCH_INSTALL "Add install rules so this library can be installed/find_package'd" OFF)

# ---------------------------------------------------------------------------
# itch: header-only library (include/itch/{parser,order_book,itch}.hpp).
# Both headers are template-driven (compile-time Handler dispatch, no vtable)
# so there is nothing to compile into this library itself.
# ---------------------------------------------------------------------------
add_library(itch INTERFACE)
add_library(itch::itch ALIAS itch)

target_include_directories(itch INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
    $<INSTALL_INTERFACE:include>
    $<INSTALL_INTERFACE:../third_party>
)
target_compile_features(itch INTERFACE cxx_std_20)

# order_book.hpp reaches third_party/ankerl/unordered_dense.h via a relative
# #include (../../third_party/...), so no include dir is needed for that on
# its own — third_party/ just has to sit next to include/ on disk, as it does
# here and in the install layout below.

# ---------------------------------------------------------------------------
# Warnings/flags for this repo's own binaries only (not forced on consumers
# of the itch:: library target).
# ---------------------------------------------------------------------------
add_library(itch_dev_flags INTERFACE)
target_compile_options(itch_dev_flags INTERFACE -Wall -Wextra -Wconversion)

if(ITCH_ENABLE_NATIVE_ARCH)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-march=native" ITCH_HAS_MARCH_NATIVE)
    if(ITCH_HAS_MARCH_NATIVE)
        target_compile_options(itch_dev_flags INTERFACE -march=native)
    endif()
endif()

# ---------------------------------------------------------------------------
# apps/ — executables that consume the library.
# ---------------------------------------------------------------------------
if(ITCH_BUILD_APPS)
    add_executable(itch_feed apps/feed.cpp)
    target_link_libraries(itch_feed PRIVATE itch::itch itch_dev_flags)

    add_executable(itch_bench apps/bench.cpp)
    target_link_libraries(itch_bench PRIVATE itch::itch itch_dev_flags)
endif()

# ---------------------------------------------------------------------------
# tests/ — unit tests, registered with CTest.
# ---------------------------------------------------------------------------
if(ITCH_BUILD_TESTS)
    enable_testing()
    add_executable(itch_test tests/test_order_book.cpp)
    target_link_libraries(itch_test PRIVATE itch::itch itch_dev_flags)
    add_test(NAME itch_test COMMAND itch_test)
endif()

# ---------------------------------------------------------------------------
# Optional install rules, for consuming this repo via find_package(itch).
# Off by default: enable with -DITCH_INSTALL=ON when you actually want to
# `cmake --install`. third_party/ is installed alongside include/ so the
# headers' relative #include to ankerl still resolves after install.
# ---------------------------------------------------------------------------
if(ITCH_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS itch EXPORT itchTargets)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY third_party/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/../third_party)

    install(EXPORT itchTargets
        FILE itchTargets.cmake
        NAMESPACE itch::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/itch)

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/itchConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/itchConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/itch)

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/itchConfig.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/itch)
endif()
