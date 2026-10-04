cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(
    Aurore
    VERSION 0.1.0
    DESCRIPTION "A from-scratch Minecraft-compatible server"
    LANGUAGES CXX
)

include(CTest)
include(CheckCXXSourceCompiles)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    set(
        CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

set(
    CMAKE_RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
)

set(
    CMAKE_LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
)

set(
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/lib"
)

foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
    string(
        TOUPPER
        "${configuration}"
        configuration_upper
    )

    set(
        "CMAKE_RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
        "${CMAKE_BINARY_DIR}/bin"
    )

    set(
        "CMAKE_LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}"
        "${CMAKE_BINARY_DIR}/bin"
    )

    set(
        "CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${configuration_upper}"
        "${CMAKE_BINARY_DIR}/lib"
    )
endforeach()

if(UNIX AND NOT APPLE)
    set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
    set(CMAKE_BUILD_RPATH "$ORIGIN")
endif()

option(
    AURORE_BUILD_TESTS
    "Build the Aurore test suite"
    ON
)

option(
    AURORE_ALLOW_MISSING_PLATFORM_BACKEND
    "Allow Aurore to compile without a production network backend. Porting only."
    OFF
)

# --------------------------------------------------------------------------
# Platform contract
# --------------------------------------------------------------------------

set(AURORE_PLATFORM_WINDOWS OFF)
set(AURORE_PLATFORM_LINUX OFF)
set(AURORE_PLATFORM_MACOS OFF)

if(WIN32)
    set(AURORE_PLATFORM_WINDOWS ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(AURORE_PLATFORM_LINUX ON)
elseif(APPLE)
    set(AURORE_PLATFORM_MACOS ON)
else()
    message(
        FATAL_ERROR
        "Aurore does not currently recognize platform '${CMAKE_SYSTEM_NAME}'."
    )
endif()

# --------------------------------------------------------------------------
# Production network backends
# --------------------------------------------------------------------------

set(AURORE_NETWORK_BACKEND_IOCP OFF)
set(AURORE_NETWORK_BACKEND_EPOLL OFF)
set(AURORE_NETWORK_BACKEND_KQUEUE OFF)

if(AURORE_PLATFORM_WINDOWS)
    set(AURORE_NETWORK_BACKEND_IOCP ON)
endif()

if(AURORE_PLATFORM_LINUX)
    set(AURORE_NETWORK_BACKEND_EPOLL ON)
endif()

set(aurore_network_backend_count 0)

foreach(
    backend
    AURORE_NETWORK_BACKEND_IOCP
    AURORE_NETWORK_BACKEND_EPOLL
    AURORE_NETWORK_BACKEND_KQUEUE
)
    if(${backend})
        math(
            EXPR
            aurore_network_backend_count
            "${aurore_network_backend_count} + 1"
        )
    endif()
endforeach()

if(aurore_network_backend_count GREATER 1)
    message(
        FATAL_ERROR
        "More than one native Aurore network backend was selected."
    )
endif()

if(
    aurore_network_backend_count EQUAL 0
    AND NOT AURORE_ALLOW_MISSING_PLATFORM_BACKEND
)
    message(
        FATAL_ERROR
        "No production network backend exists for this platform. "
        "For portability work only, configure with "
        "-DAURORE_ALLOW_MISSING_PLATFORM_BACKEND=ON."
    )
endif()

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/AuroreBuildConfig.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/Aurore/Build/Config.hpp"
)

add_library(aurore_build_options INTERFACE)
add_library(Aurore::BuildOptions ALIAS aurore_build_options)

target_compile_features(
    aurore_build_options
    INTERFACE
    cxx_std_23
)

target_include_directories(
    aurore_build_options
    INTERFACE
    "${CMAKE_BINARY_DIR}/generated"
)

if(MSVC)
    target_compile_options(
        aurore_build_options
        INTERFACE
        /W4
        /permissive-
        /EHsc
        /utf-8
        /Zc:preprocessor
    )

    target_compile_definitions(
        aurore_build_options
        INTERFACE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
else()
    target_compile_options(
        aurore_build_options
        INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# Aurore already relies on these facilities. Fail during configuration with a
# useful message rather than producing a cryptic compiler error halfway through
# the project.
check_cxx_source_compiles(
[=[
#include <chrono>
#include <expected>
#include <format>
#include <string>

int main() {
    std::expected<int, int> value{ 42 };

    const auto now =
        std::chrono::floor<std::chrono::seconds>(
            std::chrono::system_clock::now());

    const std::string text =
        std::format(
            "{} {:%H:%M:%S}",
            *value,
            now);

    return text.empty();
}
]=]
    AURORE_CXX23_STDLIB_SUPPORTED
)

if(NOT AURORE_CXX23_STDLIB_SUPPORTED)
    message(
        FATAL_ERROR
        "The selected compiler/standard library does not provide the "
        "C++23 facilities currently required by Aurore, including "
        "std::expected and std::format chrono support."
    )
endif()

find_package(Threads REQUIRED)

# --------------------------------------------------------------------------
# Pinned source dependencies
# --------------------------------------------------------------------------

set(SONNET_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(SONNET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SONNET_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(vendor/Sonnet)

if(AURORE_BUILD_TESTS)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()

    add_subdirectory(vendor/googletest)
endif()

# --------------------------------------------------------------------------
# Aurore targets
# --------------------------------------------------------------------------

add_subdirectory(Aurore.Util)
add_subdirectory(Aurore.Network)
add_subdirectory(Aurore.Protocol)
add_subdirectory(Aurore.World)
add_subdirectory(Aurore.Core)
add_subdirectory(Aurore.Server)

if(AURORE_BUILD_TESTS)
    add_subdirectory(Aurore.Tests)
endif()
