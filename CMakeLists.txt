cmake_minimum_required(VERSION 3.20)
project(marchiver LANGUAGES C CXX OBJC)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE sources src/*.cpp )

set(MW_BUILD_SHARED OFF)
set(MW_BUILD_STATIC ON)
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

# Only archive_static is needed; skip libarchive's CLI tools, tests and
# install rules. EXCLUDE_FROM_ALL keeps its shared library out of the build.
set(ENABLE_TAR OFF)
set(ENABLE_CPIO OFF)
set(ENABLE_CAT OFF)
set(ENABLE_UNZIP OFF)
set(ENABLE_TEST OFF)
set(ENABLE_INSTALL OFF)

# Don't let libarchive pick up system libraries; it has to build for targets
# like classic Mac OS where there are none. Compression/crypto libraries get
# added back as statically linked submodules.
foreach(dep MBEDTLS NETTLE OPENSSL MD LIBB2 LZ4 LZO LZMA ZSTD ZLIB BZip2
            LIBXML2 BSDXML EXPAT WIN32_XMLLITE PCREPOSIX PCRE2POSIX LIBGCC CNG
            XATTR ACL ICONV)
  set(ENABLE_${dep} OFF)
endforeach()
set(POSIX_REGEX_LIB NONE CACHE STRING "" FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/submodules/libarchive
  EXCLUDE_FROM_ALL)

# Copy only the icons that src/icon.cpp's icon_files table references.
# Reconfigures whenever icon.cpp changes so the copied set stays in sync.
set(ICON_DIR ${CMAKE_CURRENT_BINARY_DIR}/icons)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/icon.cpp)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/src/icon.cpp icon_lines
  REGEX "\"[A-Za-z0-9_.-]+\\.png\"")
set(used_icons)
foreach(line ${icon_lines})
  string(REGEX MATCH "\"([A-Za-z0-9_.-]+\\.png)\"" _ "${line}")
  list(APPEND used_icons ${CMAKE_MATCH_1})
endforeach()
list(REMOVE_DUPLICATES used_icons)
file(REMOVE_RECURSE ${ICON_DIR})
foreach(icon ${used_icons})
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/icons/${icon} ${ICON_DIR}/${icon}
    COPYONLY)
endforeach()

add_executable(${PROJECT_NAME} ${sources})

# ost_decompress.h isn't part of openstuffit's public headers.
target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSTUFFIT_DIR}/src)
target_compile_definitions(${PROJECT_NAME}
  PRIVATE MARCHIVER_MAGIC_DATABASE="${MAGIC_DATABASE}"
          MARCHIVER_ICON_DIR="${ICON_DIR}")
target_link_libraries(${PROJECT_NAME} Mw openstuffit magic archive_static)
add_dependencies(${PROJECT_NAME} magic_database)
