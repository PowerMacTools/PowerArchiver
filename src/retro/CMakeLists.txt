# Stand-ins for POSIX functions that Retro68's C library doesn't have, so the
# vendored C libraries compile and link for Classic Mac OS.

add_library(retro_compat_headers INTERFACE)
target_include_directories(retro_compat_headers
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(retro_compat_headers INTERFACE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/retro_compat.h")

add_library(retro_compat OBJECT retro_compat.c)
target_link_libraries(retro_compat PRIVATE retro_compat_headers)

# Henry Spencer's regex library, from newlib, which Retro68 doesn't build.
# libmagic needs it for its regex tests. engine.c is included by regexec.c.
target_sources(retro_compat PRIVATE
  regex/regcomp.c regex/regexec.c regex/regerror.c regex/regfree.c
  regex/collate_stub.c)
set_source_files_properties(
  regex/regcomp.c regex/regexec.c regex/regerror.c regex/regfree.c
  regex/collate_stub.c
  PROPERTIES COMPILE_DEFINITIONS _POSIX2_RE_DUP_MAX=255)
