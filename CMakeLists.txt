cmake_minimum_required(VERSION 3.16)

project(node_graph_format LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(node_graph_format STATIC
    src/NodeGraph.cpp
)

target_include_directories(node_graph_format
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(node_graph_format PUBLIC cxx_std_20)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    option(NODE_GRAPH_FORMAT_BUILD_TESTS "Build node-graph-format tests" OFF)
endif()

if(NODE_GRAPH_FORMAT_BUILD_TESTS AND BUILD_TESTING)
    find_package(Catch2 3 QUIET)

    if(NOT Catch2_FOUND)
        include(FetchContent)

        FetchContent_Declare(Catch2
            URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.4.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )

        FetchContent_MakeAvailable(Catch2)
    endif()

    add_executable(node_graph_format_tests
        tests/TestNodeGraph.cpp
    )

    target_link_libraries(node_graph_format_tests
        PRIVATE
            node_graph_format
            Catch2::Catch2WithMain
    )

    add_test(NAME node_graph_format_tests COMMAND node_graph_format_tests)
endif()

set_target_properties(node_graph_format PROPERTIES FOLDER runtime)
