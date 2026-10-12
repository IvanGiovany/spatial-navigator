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

# Locate Tree-sitter C grammar parser source
file(GLOB TREE_SITTER_C_SOURCES "/usr/src/tree-sitter/c/*/parser/src/parser.c")
if (TREE_SITTER_C_SOURCES)
    list(GET TREE_SITTER_C_SOURCES 0 TREE_SITTER_C_SRC)
    get_filename_component(TREE_SITTER_C_DIR "${TREE_SITTER_C_SRC}" DIRECTORY)
    add_library(tree_sitter_c_grammar STATIC ${TREE_SITTER_C_SRC})
    target_include_directories(tree_sitter_c_grammar PUBLIC ${TREE_SITTER_C_DIR})
    set_target_properties(tree_sitter_c_grammar PROPERTIES COMPILE_FLAGS "-w")
    list(APPEND EXTRA_LIBS tree_sitter_c_grammar)
else()
    find_library(TREE_SITTER_C_LIB NAMES tree-sitter-c)
    if (TREE_SITTER_C_LIB)
        list(APPEND EXTRA_LIBS ${TREE_SITTER_C_LIB})
    else()
        message(FATAL_ERROR "Could not find tree-sitter C grammar source (/usr/src/tree-sitter/c/*/parser/src/parser.c) or library")
    endif()
endif()

add_executable(spatial_daemon
    src/main.c
    src/parser.c
    src/graph.c
    src/render.c
    src/math3d.c
    src/font.c
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

# Test executable for AST parser & graph
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_parser.c")
    add_executable(test_parser
        tests/test_parser.c
        src/parser.c
        src/graph.c
        src/math3d.c
    )
    target_include_directories(test_parser PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${EXTRA_INCLUDES}
    )
    target_link_libraries(test_parser PRIVATE
        ${EXTRA_LIBS}
        Threads::Threads
        m
    )
endif()

# Test executable for 3D physics engine (Fruchterman-Reingold)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_physics.c")
    add_executable(test_physics
        tests/test_physics.c
        src/graph.c
        src/math3d.c
    )
    target_include_directories(test_physics PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${EXTRA_INCLUDES}
    )
    target_link_libraries(test_physics PRIVATE
        Threads::Threads
        m
    )
endif()

# Test executable for Software Rasterizer & Sixel (Phase 4 / V2)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_raster.c")
    add_executable(test_raster
        tests/test_raster.c
        src/render.c
        src/graph.c
        src/math3d.c
        src/font.c
    )
    target_include_directories(test_raster PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${EXTRA_INCLUDES}
    )
    target_link_libraries(test_raster PRIVATE
        ${EXTRA_LIBS}
        Threads::Threads
        m
    )
endif()

