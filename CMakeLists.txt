cmake_minimum_required(VERSION 3.24)
project(TitanBtrfs VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler definitions and flags
if (WIN32)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

if (MSVC)
    add_compile_options(/W4 /permissive- /Zc:preprocessor)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-variable -Wno-unused-but-set-variable)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-Wno-packed-not-aligned)
    endif()
    if (WIN32)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Standalone TitanBtrfs Core Library
add_library(titanbtrfs_core INTERFACE)
target_include_directories(titanbtrfs_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Test Suite Executable
add_executable(titanbtrfs_tests tests/test_btrfs.cpp)
target_link_libraries(titanbtrfs_tests PRIVATE titanbtrfs_core)

# CLI Formatting Tool
add_executable(titanbtrfs_format src/tools/titanbtrfs_format.cpp)
target_link_libraries(titanbtrfs_format PRIVATE titanbtrfs_core)

# CLI Inspection & Scrubber Tool
add_executable(titanbtrfs_check src/tools/titanbtrfs_check.cpp)
target_link_libraries(titanbtrfs_check PRIVATE titanbtrfs_core)

# Background Service Daemon
add_executable(titanbtrfs_svc src/service/titanbtrfs_svc.cpp)
target_link_libraries(titanbtrfs_svc PRIVATE titanbtrfs_core)
if (WIN32)
    target_link_libraries(titanbtrfs_svc PRIVATE advapi32)
endif()

if (WIN32)
    # WinFsp Mount Driver Bridge
    add_executable(titanbtrfs_mount src/mount/winfsp_mount.cpp)
    target_link_libraries(titanbtrfs_mount PRIVATE titanbtrfs_core)

    # Standalone Installer Packager
    add_executable(titanbtrfs_packager src/tools/titanbtrfs_packager.cpp)
    target_link_libraries(titanbtrfs_packager PRIVATE titanbtrfs_core)

    # Standalone 1-Click GUI Installer
    add_executable(titanbtrfs_setup WIN32 src/installer/titanbtrfs_setup.cpp)
    target_link_libraries(titanbtrfs_setup PRIVATE titanbtrfs_core user32 gdi32 gdiplus comctl32 dwmapi uxtheme ole32 oleaut32 shell32 advapi32)
    add_dependencies(titanbtrfs_setup titanbtrfs_format titanbtrfs_check titanbtrfs_mount titanbtrfs_svc titanbtrfs_packager)

    set(WINFSP_MSI "${CMAKE_CURRENT_SOURCE_DIR}/redist/winfsp-2.1.25156.msi")
    set(PACKAGER_EXTRA_ARGS "")
    if (EXISTS "${WINFSP_MSI}")
        set(PACKAGER_EXTRA_ARGS "${WINFSP_MSI}")
    endif()

    add_custom_command(
        TARGET titanbtrfs_setup POST_BUILD
        COMMAND titanbtrfs_packager $<TARGET_FILE:titanbtrfs_setup>
                $<TARGET_FILE:titanbtrfs_format>
                $<TARGET_FILE:titanbtrfs_check>
                $<TARGET_FILE:titanbtrfs_mount>
                $<TARGET_FILE:titanbtrfs_svc>
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/titanbtrfs_hero.jpg
                ${PACKAGER_EXTRA_ARGS}
        COMMENT "Embedding all binaries, WinFsp MSI, and assets into standalone titanbtrfs_setup.exe..."
    )
endif()

# ============================================================================
# CPack Packaging Configuration
# ============================================================================
set(CPACK_PACKAGE_NAME "TitanBtrfs")
set(CPACK_PACKAGE_VENDOR "MicaNT-Kernel")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Btrfs File System Engine, WinFsp Mount Bridge, and Subvolume Manager for Windows NT")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

install(TARGETS titanbtrfs_tests titanbtrfs_format titanbtrfs_check titanbtrfs_svc RUNTIME DESTINATION bin)
if (WIN32)
    install(TARGETS titanbtrfs_mount titanbtrfs_setup RUNTIME DESTINATION bin)
    install(DIRECTORY assets/ DESTINATION assets)
    install(DIRECTORY redist/ DESTINATION redist OPTIONAL)
endif()

set(CPACK_GENERATOR "ZIP")
include(CPack)

# CTest registration
enable_testing()
add_test(NAME TitanBtrfsCoreTests COMMAND titanbtrfs_tests)
