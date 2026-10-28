cmake_minimum_required(VERSION 3.20)
project(marchiver LANGUAGES C CXX OBJC)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE sources src/*.cpp )

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/submodules/milsko)

# openstuffit only ships a Makefile, so build its library sources here.
# openstuffit.c is the CLI's main() and is left out.
set(OPENSTUFFIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/submodules/openstuffit)
file(GLOB openstuffit_sources ${OPENSTUFFIT_DIR}/src/ost_*.c)
find_package(ZLIB REQUIRED)
add_library(openstuffit STATIC ${openstuffit_sources})
set_target_properties(openstuffit PROPERTIES C_STANDARD 99)
target_compile_definitions(openstuffit PRIVATE _FILE_OFFSET_BITS=64)
target_include_directories(openstuffit
  PUBLIC ${OPENSTUFFIT_DIR}/include
  PRIVATE ${OPENSTUFFIT_DIR}/src)
target_link_libraries(openstuffit PUBLIC ZLIB::ZLIB)

add_subdirectory(cmake/libmagic)

add_executable(${PROJECT_NAME} ${sources})

# ost_decompress.h isn't part of openstuffit's public headers.
target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSTUFFIT_DIR}/src)
target_compile_definitions(${PROJECT_NAME}
  PRIVATE MARCHIVER_MAGIC_DATABASE="${MAGIC_DATABASE}")
target_link_libraries(${PROJECT_NAME} Mw openstuffit magic)
add_dependencies(${PROJECT_NAME} magic_database)
