cmake_minimum_required(VERSION 3.2)
project(linenoise)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.c) 

use_c11()

add_library(linenoise STATIC ${SOURCES})

add_library(mdv::linenoise ALIAS linenoise)

target_include_directories(linenoise PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
