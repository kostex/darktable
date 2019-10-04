include(CheckCompilerFlagAndEnableIt)
include(CheckCCompilerFlagAndEnableIt)
include(CheckCXXCompilerFlagAndEnableIt)

CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wall)

if(WIN32)
  # MSYS2 gcc compiler gives false positive warnings for (format (printf, 1, 2) - need to turn off for the time being
  CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wno-format)
else()
  CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wformat)
  CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wformat-security)
endif()

# cleanup this once we no longer need to support gcc-4.9
if(NOT (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_LESS 5.0))
  CHECK_C_COMPILER_FLAG_AND_ENABLE_IT(-Wshadow)
endif()
if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.0))
  CHECK_CXX_COMPILER_FLAG_AND_ENABLE_IT(-Wshadow)
endif()

CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wtype-limits)

CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wvla)

CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wold-style-declaration)

CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wthread-safety)

# since checking if defined(__GNUC__) is not enough to prevent Clang from using GCC-specific pragmas
# (so Clang defines __GNUC__ ???) we need to disable the warnings about unknown pragmas
CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wno-unknown-pragmas)

# may be our bug :(
CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wno-error=varargs)

# need proper gcc7 to try to fix all the warnings.
# so just disable for now.
CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wno-format-truncation)

# clang-4.0 bug https://llvm.org/bugs/show_bug.cgi?id=28115#c7
CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wno-error=address-of-packed-member)

# should be < 64Kb
math(EXPR MAX_MEANINGFUL_SIZE 32*1024)
CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wframe-larger-than=${MAX_MEANINGFUL_SIZE})
CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wstack-usage=${MAX_MEANINGFUL_SIZE})

# 512Kb
# # src/external/wb_presets.c, wb_preset <- ~400Kb
math(EXPR MAX_MEANINGFUL_SIZE 512*1024)
CHECK_COMPILER_FLAG_AND_ENABLE_IT(-Wlarger-than=${MAX_MEANINGFUL_SIZE})

# minimal main thread's stack/frame stack size. (musl)
# 1Mb seems enough, and 256Kb seems to work too.
# 128Kb does NOT work, based on my testing. Roman.
# MUST be a multiple of the system page size !!!
# see  $ getconf PAGESIZE
math(EXPR WANTED_STACK_SIZE 64*4*1024)

# minimal pthread stack/frame stack size. (musl)
# 256Kb seems to work.
# 128Kb does NOT work, based on my testing. Roman.
# MUST be a multiple of the system page size !!!
# see  $ getconf PAGESIZE
math(EXPR WANTED_THREADS_STACK_SIZE 64*4*1024)

if(SOURCE_PACKAGE)
  add_definitions(-D_RELEASE)
endif()

###### GTK+3 ######
#
#  Do not include individual headers
#
add_definitions(-DGTK_DISABLE_SINGLE_INCLUDES)

#
# Dirty hack to enforce GTK3 behaviour in GTK2: "Replace GDK_<keyname> with GDK_KEY_<keyname>"
#
add_definitions(-D__GDK_KEYSYMS_COMPAT_H__)

#
#  Do not use deprecated symbols
#
add_definitions(-DGDK_DISABLE_DEPRECATED)
add_definitions(-DGTK_DISABLE_DEPRECATED)
###### GTK+3 port ######
