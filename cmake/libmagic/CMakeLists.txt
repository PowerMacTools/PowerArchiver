# file/libmagic only ships autotools, so build it here. This produces the
# `magic` library, the `file` tool (needed only to compile the database) and
# magic.mgc in this directory's binary dir, exposed as MAGIC_DATABASE.

include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckStructHasMember)
include(CheckTypeSize)

set(FILE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../submodules/file)

file(STRINGS ${FILE_DIR}/configure.ac ac_init REGEX "^AC_INIT")
string(REGEX MATCH "\\[([0-9.]+)\\]" _ "${ac_init}")
set(MAGIC_VERSION ${CMAKE_MATCH_1})

list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)

foreach(header byteswap.h dirent.h getopt.h inttypes.h regex.h spawn.h
               stdint.h sys/bswap.h sys/ioctl.h sys/mman.h sys/sysmacros.h
               sys/time.h sys/utime.h sys/wait.h unistd.h utime.h wchar.h
               wctype.h xlocale.h)
  string(TOUPPER "HAVE_${header}" var)
  string(REGEX REPLACE "[/.]" "_" var "${var}")
  check_include_file(${header} ${var})
endforeach()

set(func_headers "stdio.h;stdlib.h;string.h;strings.h;time.h;unistd.h;\
signal.h;locale.h;wchar.h;spawn.h;sys/select.h;sys/ioctl.h;sys/mman.h;\
sys/time.h;utime.h")
foreach(func asctime_r asprintf ctime_r dprintf fmtcheck fork freelocale
             getline gmtime_r ioctl localtime_r mbrtowc memmem mkstemp mmap
             newlocale pathconf pipe pipe2 posix_spawnp pread readlink
             reallocarray select sigaction strcasestr strlcat strlcpy strndup
             strtof tzset uselocale utime utimes vasprintf wcwidth)
  string(TOUPPER "HAVE_${func}" var)
  check_symbol_exists(${func} "${func_headers}" ${var})
endforeach()
set(HAVE_DECL_FORK ${HAVE_FORK})
check_symbol_exists(getopt_long getopt.h HAVE_GETOPT_LONG)
check_symbol_exists(major sys/sysmacros.h HAVE_MAJOR)

set(CMAKE_EXTRA_INCLUDE_FILES signal.h)
check_type_size(sig_t SIG_T)
set(CMAKE_EXTRA_INCLUDE_FILES getopt.h)
check_type_size("struct option" STRUCT_OPTION)
unset(CMAKE_EXTRA_INCLUDE_FILES)
check_struct_has_member("struct stat" st_rdev sys/stat.h
                        HAVE_STRUCT_STAT_ST_RDEV)
check_struct_has_member("struct stat" st_flags sys/stat.h
                        HAVE_STRUCT_STAT_ST_FLAGS)
check_struct_has_member("struct tm" tm_gmtoff time.h HAVE_STRUCT_TM_TM_GMTOFF)
check_struct_has_member("struct tm" tm_zone time.h HAVE_STRUCT_TM_TM_ZONE)

set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/include)
configure_file(config.h.in ${gen_dir}/config.h)
string(REPLACE "." "" magic_version_no "${MAGIC_VERSION}")
file(READ ${FILE_DIR}/src/magic.h.in magic_h)
string(REPLACE "X.YY" "${magic_version_no}" magic_h "${magic_h}")
file(GENERATE OUTPUT ${gen_dir}/magic.h CONTENT "${magic_h}")

set(magic_sources
  buffer.c magic.c apprentice.c softmagic.c ascmagic.c encoding.c compress.c
  is_csv.c is_json.c is_simh.c is_tar.c readelf.c print.c fsmagic.c funcs.c
  apptype.c der.c cdf.c cdf_time.c readcdf.c swap.c)
list(TRANSFORM magic_sources PREPEND ${FILE_DIR}/src/)

# Replacements for functions the platform lacks.
foreach(func asctime_r asprintf ctime_r dprintf fmtcheck getline gmtime_r
             localtime_r pread reallocarray strcasestr strlcat strlcpy
             vasprintf)
  string(TOUPPER "HAVE_${func}" var)
  if(NOT ${var})
    list(APPEND magic_sources ${FILE_DIR}/src/missing/${func}.c)
  endif()
endforeach()

add_library(magic STATIC ${magic_sources})
target_compile_definitions(magic PRIVATE HAVE_CONFIG_H)
target_include_directories(magic
  PUBLIC ${gen_dir}
  PRIVATE ${FILE_DIR}/src)
target_link_libraries(magic PRIVATE m)

set(file_sources ${FILE_DIR}/src/file.c)
if(NOT HAVE_GETOPT_LONG)
  list(APPEND file_sources ${FILE_DIR}/src/missing/getopt_long.c)
endif()
add_executable(file ${file_sources})
target_compile_definitions(file PRIVATE HAVE_CONFIG_H)
target_include_directories(file PRIVATE ${FILE_DIR}/src)
target_link_libraries(file PRIVATE magic)

# Compile the magic database. `file -C -m magic` writes magic.mgc next to
# the input directory.
file(GLOB magic_fragments ${FILE_DIR}/magic/Magdir/*)
set(MAGIC_DATABASE ${CMAKE_CURRENT_BINARY_DIR}/magic.mgc)
set(MAGIC_DATABASE ${MAGIC_DATABASE} PARENT_SCOPE)
add_custom_command(
  OUTPUT ${MAGIC_DATABASE}
  COMMAND ${CMAKE_COMMAND} -E rm -rf magic
  COMMAND ${CMAKE_COMMAND} -E make_directory magic
  COMMAND ${CMAKE_COMMAND} -E copy ${FILE_DIR}/magic/Header
          ${FILE_DIR}/magic/Localstuff ${magic_fragments} magic
  COMMAND file -C -m magic
  COMMAND ${CMAKE_COMMAND} -E rm -rf magic
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS file ${magic_fragments}
  COMMENT "Compiling magic database"
  VERBATIM)
add_custom_target(magic_database DEPENDS ${MAGIC_DATABASE})
