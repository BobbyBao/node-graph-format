cmake_minimum_required(VERSION 3.16)

project(node_graph_format LANGUAGES CXX)

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

set_target_properties(node_graph_format PROPERTIES FOLDER runtime)
