cmake_minimum_required(VERSION 3.20)
project(PowerArchiver LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE sources src/*.cpp )

set(MW_BUILD_SHARED OFF)
set(MW_BUILD_STATIC ON)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/submodules/milsko)

if(CMAKE_SYSTEM_NAME MATCHES Retro)
  add_subdirectory(src/retro)
endif()

add_subdirectory(cmake/libarchive)

# openstuffit only ships a Makefile, so build its library sources here.
# openstuffit.c is the CLI's main() and ost_write.c only creates archives, so
# both are left out.
set(OPENSTUFFIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/submodules/openstuffit)
file(GLOB openstuffit_sources ${OPENSTUFFIT_DIR}/src/ost_*.c)
list(REMOVE_ITEM openstuffit_sources ${OPENSTUFFIT_DIR}/src/ost_write.c)
add_library(openstuffit STATIC ${openstuffit_sources})
set_target_properties(openstuffit PROPERTIES C_STANDARD 99)
target_compile_definitions(openstuffit PRIVATE _FILE_OFFSET_BITS=64)
target_include_directories(openstuffit
  PUBLIC ${OPENSTUFFIT_DIR}/include
  PRIVATE ${OPENSTUFFIT_DIR}/src)
target_link_libraries(openstuffit PUBLIC zlib)
if(TARGET retro_compat_headers)
  target_link_libraries(openstuffit PRIVATE retro_compat_headers)
endif()

add_subdirectory(cmake/libmagic)

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
list(APPEND used_icons "PowerArchiver_Icon.png")
list(REMOVE_DUPLICATES used_icons)
file(REMOVE_RECURSE ${ICON_DIR})
foreach(icon ${used_icons})
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/icons/${icon} ${ICON_DIR}/${icon}
    COPYONLY)
endforeach()

if(CMAKE_SYSTEM_NAME MATCHES Retro)
  # The Finder icon's resources, from a 32x32 PNG
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(icon_png ${CMAKE_CURRENT_SOURCE_DIR}/icons/PowerArchiver_Icon.png)
  set(icon_r ${CMAKE_CURRENT_BINARY_DIR}/icon.r)
  add_custom_command(
    OUTPUT ${icon_r}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/mac_icon.py rez
            ${icon_png} ${icon_r}
    DEPENDS ${icon_png} ${CMAKE_CURRENT_SOURCE_DIR}/tools/mac_icon.py
    COMMENT "Generating icon.r"
    VERBATIM)

  # the creator code matches the BNDL in marchiver.r
  add_application(${PROJECT_NAME} ${sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/retro/marchiver.r ${icon_r}
    CREATOR PwAr)
  target_link_libraries(${PROJECT_NAME} retro_compat)
else()
  add_executable(${PROJECT_NAME} ${sources})
endif()

target_compile_options(${PROJECT_NAME} PRIVATE -fno-exceptions)

# ost_decompress.h isn't part of openstuffit's public headers.
target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSTUFFIT_DIR}/src)
target_link_libraries(${PROJECT_NAME} Mw openstuffit magic archive_static)
add_dependencies(${PROJECT_NAME} magic_database)

if(NOT CMAKE_SYSTEM_NAME MATCHES Retro)
  target_compile_definitions(${PROJECT_NAME}
    PRIVATE MARCHIVER_MAGIC_DATABASE="${MAGIC_DATABASE}"
            MARCHIVER_ICON_DIR="${ICON_DIR}")
else()
  target_compile_definitions(${PROJECT_NAME}
    PRIVATE MARCHIVER_MAGIC_DATABASE=":magic.mgc"
            MARCHIVER_ICON_DIR=":icons")

  find_program(HFORMAT hformat)
  find_program(HMOUNT hmount)
  find_program(HCOPY hcopy)
  find_program(HMKDIR hmkdir)
  find_program(HUMOUNT humount)
  if(HFORMAT AND HMOUNT AND HCOPY AND HMKDIR AND HUMOUNT)
    # add_application() already makes ${PROJECT_NAME}.dsk, with only the
    # application on it
    set(disk ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-full.dsk)
    set(copy_icons)
    foreach(icon ${used_icons})
      list(APPEND copy_icons
        COMMAND ${HCOPY} -r ${ICON_DIR}/${icon} :icons:${icon})
    endforeach()
    add_custom_command(
      OUTPUT ${disk}
      COMMAND ${CMAKE_COMMAND} -E rm -f ${disk}
      COMMAND dd if=/dev/zero of=${disk} bs=1M count=32 status=none
      COMMAND ${HFORMAT} -l ${PROJECT_NAME} ${disk}
      # Rez can't set the "has bundle" Finder flag, without which the Finder
      # ignores the application's icon, so set it in a copy of the MacBinary
      # file. Only the application gets an icon; the volume keeps the default.
      # hcopy -m unpacks MacBinary, keeping the resource fork and Finder info.
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/mac_icon.py bundle
              ${PROJECT_NAME}.bin ${PROJECT_NAME}-bundle.bin
      COMMAND ${HCOPY} -m ${PROJECT_NAME}-bundle.bin :${PROJECT_NAME}
      COMMAND ${HCOPY} -r ${MAGIC_DATABASE} :magic.mgc
      COMMAND ${HMKDIR} :icons
      ${copy_icons}
      COMMAND ${HUMOUNT}
      # the target orders the build, the file remakes the disk when it changes
      DEPENDS ${PROJECT_NAME}_APPL ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
              ${MAGIC_DATABASE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/mac_icon.py
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Making ${PROJECT_NAME}-full.dsk"
      VERBATIM)
    add_custom_target(disk_image ALL DEPENDS ${disk})
  else()
    message(STATUS "hfsutils not found, so no disk image will be made")
  endif()
endif()
