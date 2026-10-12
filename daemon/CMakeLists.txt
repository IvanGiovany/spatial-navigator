cmake_minimum_required(VERSION 3.16)
project(spatial_daemon C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_compile_options(-Wall -Wextra -Wpedantic)

# Locate pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Locate tree-sitter and libsixel via pkg-config or fallback
find_package(PkgConfig QUIET)

set(EXTRA_LIBS "")
set(EXTRA_INCLUDES "")

if (PKG_CONFIG_FOUND)
    pkg_check_modules(TREE_SITTER tree-sitter)
    pkg_check_modules(SIXEL libsixel)
endif()

if (TREE_SITTER_FOUND)
    list(APPEND EXTRA_INCLUDES ${TREE_SITTER_INCLUDE_DIRS})
    list(APPEND EXTRA_LIBS ${TREE_SITTER_LIBRARIES})
else()
    find_path(TREE_SITTER_INCLUDE_DIR tree_sitter/api.h)
    find_library(TREE_SITTER_LIBRARY NAMES tree-sitter)
    if (NOT TREE_SITTER_LIBRARY OR NOT TREE_SITTER_INCLUDE_DIR)
        message(FATAL_ERROR "Could not find tree-sitter library or headers")
    endif()
    list(APPEND EXTRA_INCLUDES ${TREE_SITTER_INCLUDE_DIR})
    list(APPEND EXTRA_LIBS ${TREE_SITTER_LIBRARY})
endif()

if (SIXEL_FOUND)
    list(APPEND EXTRA_INCLUDES ${SIXEL_INCLUDE_DIRS})
    list(APPEND EXTRA_LIBS ${SIXEL_LIBRARIES})
else()
    find_path(SIXEL_INCLUDE_DIR sixel.h PATH_SUFFIXES sixel)
    find_library(SIXEL_LIBRARY NAMES sixel libsixel)
    if (NOT SIXEL_LIBRARY OR NOT SIXEL_INCLUDE_DIR)
        message(FATAL_ERROR "Could not find libsixel library or headers")
    endif()
    list(APPEND EXTRA_INCLUDES ${SIXEL_INCLUDE_DIR})
    list(APPEND EXTRA_LIBS ${SIXEL_LIBRARY})
endif()

add_executable(spatial_daemon
    src/main.c
    src/parser.c
    src/graph.c
    src/render.c
)

target_include_directories(spatial_daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${EXTRA_INCLUDES}
)

target_link_libraries(spatial_daemon PRIVATE
    ${EXTRA_LIBS}
    Threads::Threads
    m
)
