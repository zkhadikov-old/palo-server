################################################################################
###
### Copyright (C) 2006-2013 Jedox AG
###
### This program is free software; you can redistribute it and/or modify it
### under the terms of the GNU General Public License (Version 2) as published
### by the Free Software Foundation at http://www.gnu.org/copyleft/gpl.html.
###
### This program is distributed in the hope that it will be useful, but WITHOUT
### ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
### FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
### more details.
###
### You should have received a copy of the GNU General Public License along with
### this program; if not, write to the Free Software Foundation, Inc., 59 Temple
### Place, Suite 330, Boston, MA 02111-1307 USA
###
### If you are developing and distributing open source applications under the
### GPL License, then you are free to use Palo under the GPL License.  For OEMs,
### ISVs, and VARs who distribute Palo with their products, and do not license
### and distribute their source code under the GPL, Jedox provides a flexible
### OEM Commercial License.
###
### \author Zurab Khadikov, Jedox AG, Freiburg, Germany
###
###
################################################################################

# change palo version and revision here when needed
set(PALO_MAJOR 5)
set(PALO_MINOR 1)
set(REVISION "10000")

# print config message
message(STATUS "================== Configuring PALO Server Version 5.1.x ==================")

# minimum required cmake version
cmake_minimum_required(VERSION 3.10)

# set project name
project("palo" CXX C)

# Build with C++17 explicitly.
# ICU on modern Debian relies on C++17 types (e.g. std::string_view), so
# building with older standards fails.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# set path for extra cmake modules
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/Modules/CMake")

# include cmake modules
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckIncludeFiles)
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckTypeSize)
include(CheckHeaderSTDC)
include(TestBigEndian)
include(CheckCXXSourceCompiles)
include(CMakeDependentOption)
include(CheckIncludeFile)
include(CheckTypeExists)
include(UserFunctions)

# Parsing errors more efficiently extra for eclipse
if(CMAKE_COMPILER_IS_GNUCC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fmessage-length=0 -DU_USING_ICU_NAMESPACE=1")
endif(CMAKE_COMPILER_IS_GNUCC)

if(CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmessage-length=0 -DU_USING_ICU_NAMESPACE=1")
endif(CMAKE_COMPILER_IS_GNUCXX)

################################################################################
### check build system
################################################################################

message(STATUS "c compiler is: ${CMAKE_C_COMPILER_ID}")
message(STATUS "c++ compiler is: ${CMAKE_CXX_COMPILER_ID}")

################################################################################
### define variables
################################################################################

# set include files variable
set(INCLUDES "")

# libs variables
set(LIBS "")
set(OPENSSL_LIBS "")

################################################################################
### checks for programs
################################################################################

find_package(BISON)
find_package(FLEX)
find_program(GPERF_FOUND gperf PATHS /usr/bin /usr/local/bin)
if(GPERF_FOUND)
    set(ENABLE_GPERF yes)
    message(STATUS "gperf found")
endif(GPERF_FOUND)

# tells the compiler to warn about all the things it finds unusual
check_c_compiler_flag(-Wall WallC)
check_cxx_compiler_flag(-Wall WallCXX)
if(WallC AND WallCXX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif()

# Generated lexer/gperf sources still use legacy `register` and trigger
# noisy warnings with C++17; keep warnings useful by suppressing that one.
check_cxx_compiler_flag(-Wno-register NoRegisterCXX)
if(NoRegisterCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-register")
endif()

# check if C compiler doesn't accept -c and -o together
set(CCO "-c -o")
check_c_compiler_flag(${CCO} MINUS_C_MINUS_O)
if(MINUS_C_MINUS_O)
     set(NO_MINUS_C_MINUS_O YES)
else(MINUS_C_MINUS_O)
     set(NO_MINUS_C_MINUS_O NO)
endif(MINUS_C_MINUS_O)

################################################################################
#########################  USER OPTIONS  #######################################
################################################################################

set(PROJECT_BUILD_TYPE "Release" CACHE STRING "Select a build type")
set_property(CACHE PROJECT_BUILD_TYPE PROPERTY STRINGS "Release" "Debug" "RelWithDebInfo")
set(CMAKE_BUILD_TYPE ${PROJECT_BUILD_TYPE})

# user can change CMAKE_BUILD_TYPE from command-line
if(NOT (CMAKE_BUILD_TYPE STREQUAL Release OR CMAKE_BUILD_TYPE STREQUAL Debug OR CMAKE_BUILD_TYPE STREQUAL RelWithDebInfo))
    message(WARNING "only [Release],[Debug] or [RelWithDebInfo] build types allowed")
    set(CMAKE_BUILD_TYPE Release)
    message(STATUS "use default build type - ${CMAKE_BUILD_TYPE}")
else(NOT (CMAKE_BUILD_TYPE STREQUAL Release OR CMAKE_BUILD_TYPE STREQUAL Debug OR CMAKE_BUILD_TYPE STREQUAL RelWithDebInfo))
    message(STATUS "build type - ${CMAKE_BUILD_TYPE}")
endif(NOT (CMAKE_BUILD_TYPE STREQUAL Release OR CMAKE_BUILD_TYPE STREQUAL Debug OR CMAKE_BUILD_TYPE STREQUAL RelWithDebInfo))
message(STATUS "to change project build type use -DPROJECT_BUILD_TYPE={Release|Debug|RelWithDebInfo}")


if(CMAKE_BUILD_TYPE STREQUAL Release)
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
else(CMAKE_BUILD_TYPE STREQUAL Release)
    if(CMAKE_BUILD_TYPE STREQUAL RelWithDebInfo)
        set(CMAKE_C_FLAGS_RELWITHDEBINFO "-g3 -ggdb -O2 -D_DEBUG")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g3 -ggdb -O2 -D_DEBUG")
    else(CMAKE_BUILD_TYPE STREQUAL RelWithDebInfo)
        set(CMAKE_C_FLAGS_DEBUG "-gdwarf-4 -fvar-tracking-assignments -O0 -D_DEBUG")
        set(CMAKE_CXX_FLAGS_DEBUG "-gdwarf-4 -fvar-tracking-assignments -O0 -D_DEBUG")
    endif(CMAKE_BUILD_TYPE STREQUAL RelWithDebInfo)
endif(CMAKE_BUILD_TYPE STREQUAL Release)

################################################################################
### option for 32bit/64bit compile only for linux
################################################################################

option(ENABLE_32BIT "force 32bit compilation [default=OFF]" OFF)
if(ENABLE_32BIT STREQUAL ON)
    set(CMAKE_REQUIRED_FLAGS "-m32")
    check_c_compiler_flag(-m32 m32C)
    check_cxx_compiler_flag(-m32 m32CXX)
    if(m32C AND m32CXX)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
        set(ARCH_HINT "i386")
        set(CMAKE_LIBRARY_ARCHITECTURE "i386-linux-gnu")
        set(OUT_LIB_SUFFIX "")
        #message(STATUS "librarys are: ${CMAKE_LIBRARY_ARCHITECTURE}")
    else(m32C AND m32CXX)
        set(CMAKE_REQUIRED_FLAGS "")
        set(ENABLE_32BIT OFF)
        message(STATUS "no support for 32bit compilation!")
    endif(m32C AND m32CXX)
else(ENABLE_32BIT STREQUAL ON)
    set(ENABLE_32BIT OFF)
endif(ENABLE_32BIT STREQUAL ON)
message(STATUS "32bit compilation is [${ENABLE_32BIT}]")
message(STATUS "to enable/disable force 32bit compilation add -DENABLE_32BIT={ON|OFF}")

################################################################################
### define output build paths
################################################################################

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/usr/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/usr/lib${OUT_LIB_SUFFIX})
message(STATUS "binary output directory is: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "library output directory is: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

################################################################################
### check BOOST
################################################################################

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

find_package( Boost 1.43 REQUIRED COMPONENTS thread system)
if(Boost_FOUND)
    set(Boost_USE_MULTITHREADED ON)
    set(INCLUDES ${INCLUDES} ${Boost_INCLUDE_DIRS})
    set(LIBS ${LIBS} boost_thread boost_system boost_regex)
    set(HAVE_LIBBOOST_THREAD YES)
    set(HAVE_BOOST_THREAD_HPP YES)
else(Boost_FOUND)
    message(FATAL_ERROR "Boost is not found, install it!")
endif(Boost_FOUND)

################################################################################
### check openSSL (OpenSSL 3.x — target: Debian 13+ and similar)
################################################################################

find_package(OpenSSL 3.0 REQUIRED)
if(OPENSSL_FOUND)
    check_include_file_cxx(openssl/ssl.h HAVE_OPENSSL_SSL_H)
    check_library_exists(ssl SSL_get_error "" HAVE_SSL_LIB)
    set(INCLUDES ${INCLUDES} ${OpenSSL_INCLUDE_DIRS})
    set(OPENSSL_LIBS ssl crypto)
    # libssl: TLS client (NetInitialisation, HttpsClientConnection); libcrypto: digests etc.
    set(LIBS ${LIBS} ssl crypto)
else(OPENSSL_FOUND)
    message(FATAL_ERROR "OpenSSL not available, install it!")
endif(OPENSSL_FOUND)

################################################################################
### check google perf tools
################################################################################

option(ENABLE_GOOGLE_CPU_PROFILER "add -lprofiler for google cpu profiling [default=OFF]" OFF)
mark_as_advanced(ENABLE_GOOGLE_CPU_PROFILER)

find_package(Perftools COMPONENTS)
if(Perftools_FOUND)
    set(LIBS ${LIBS} tcmalloc_minimal)
    if(ENABLE_GOOGLE_CPU_PROFILER STREQUAL ON)
        set(LIBS ${LIBS} profiler)
    endif(ENABLE_GOOGLE_CPU_PROFILER STREQUAL ON)
else(Perftools_FOUND)
    message(FATAL_ERROR "Perftools not available, install it!")
endif(Perftools_FOUND)

################################################################################
### check for icu
################################################################################

find_package (ICU REQUIRED COMPONENTS uc i18n)
if(ICU_FOUND)
    set(INCLUDES ${INCLUDES} ${ICU_INCLUDE_DIRS})
    set(LIBS ${LIBS} icuuc icui18n)
else(ICU_FOUND)
    message(FATAL_ERROR "icu not available, install it or add it with -DLIB_ICU_ROOT_DIR =[path to icu]!")
endif(ICU_FOUND)

################################################################################
### check for zlib
################################################################################

find_package(ZLIB REQUIRED)
if(ZLIB_FOUND)
    set(INCLUDES ${INCLUDES} ${ZLIB_INCLUDE_DIRS})
    set(LIBS ${LIBS} z)
else(ZLIB_FOUND)
    message(FATAL_ERROR "zlib not available, install it!")
endif(ZLIB_FOUND)

################################################################################
### option for enabling the https interface as builtin or shared library
################################################################################

set(ENABLE_HTTPS "Module" CACHE STRING "Select to enable building the https interface as builtin or shared module")
set_property(CACHE ENABLE_HTTPS PROPERTY STRINGS "Builtin" "Module")

if(ENABLE_HTTPS STREQUAL Module)
    set(ENABLE_HTTPS Module)
    set(ENABLE_HTTPS_MODULE ON)
else(ENABLE_HTTPS STREQUAL Module)
    if(ENABLE_HTTPS STREQUAL Builtin)
        set(ENABLE_HTTPS Builtin)
        set(ENABLE_HTTPS_MODULE OFF)
     else(ENABLE_HTTPS STREQUAL Builtin)
        set(ENABLE_HTTPS OFF)
        set(ENABLE_HTTPS_MODULE OFF)
     endif(ENABLE_HTTPS STREQUAL Builtin)
endif(ENABLE_HTTPS STREQUAL Module)

message(STATUS "building of https interface is [${ENABLE_HTTPS}]")
message(STATUS "to enable/disable building of https interface add -DENABLE_HTTPS={Module/Builtin}")

################################################################################
#########################  ADVANCE USER OPTIONS ################################
################################################################################

################################################################################
### set use bison
################################################################################

option(ENABLE_USE_BISON "enable bison generated RuleParser [default=OFF]" OFF)
mark_as_advanced(ENABLE_USE_BISON)
# check input for this option
if(ENABLE_USE_BISON STREQUAL ON)
    set(ENABLE_USE_BISON ON)
else()
    set(ENABLE_USE_BISON OFF)
endif()
message(STATUS "bison generated RuleParser is [${ENABLE_USE_BISON}]")
message(STATUS "to enable/disable bison generated RuleParser add -DENABLE_USE_BISON={ON|OFF}")

################################################################################
### set use flex
################################################################################

option(ENABLE_USE_FLEX "enable flex gererated RuleScanner [default=OFF]" OFF)
mark_as_advanced(ENABLE_USE_FLEX)
# check input for this option
if(ENABLE_USE_FLEX STREQUAL ON)
    set(ENABLE_USE_FLEX ON)
else()
    set(ENABLE_USE_FLEX OFF)
endif()
message(STATUS "flex gererated RuleScanner is [${ENABLE_USE_FLEX}]")
message(STATUS "to enable/disable flex gererated RuleScanner add -DENABLE_USE_FLEX={ON|OFF}")

################################################################################
### set use gperf
################################################################################

option(ENABLE_USE_GPERF "enable gperf generated PaloCommands [default=OFF]" OFF)
mark_as_advanced(ENABLE_USE_GPERF)
# check input for this option
if(ENABLE_USE_GPERF STREQUAL ON)
    set(ENABLE_USE_GPERF ON)
else()
    set(ENABLE_USE_GPERF OFF)
endif()
message(STATUS "gperf generated PaloCommands is [${ENABLE_USE_GPERF}]")
message(STATUS "to enable/disable gperf generated PaloCommands add -DENABLE_USE_GPERF={ON|OFF}")

################################################################################
### set verbose build
################################################################################

set(ENABLE_VERBOSE ON CACHE BOOL "set verbose build")

if(ENABLE_VERBOSE STREQUAL ON)
    set(CMAKE_VERBOSE_MAKEFILE ON)
    set(CUDA_VERBOSE_BUILD ON)
else(ENABLE_VERBOSE STREQUAL ON)
    set(CMAKE_VERBOSE_MAKEFILE OFF)
    set(CUDA_VERBOSE_BUILD OFF)
endif(ENABLE_VERBOSE STREQUAL ON)

message(STATUS "enable verbose make files is [${CMAKE_VERBOSE_MAKEFILE}]")
message(STATUS "to enable/disable verbose build add -DENABLE_VERBOSE={ON/OFF}")

################################################################################
### option for enable trace
################################################################################

option(ENABLE_TRACE_OPTION "enable support for more trace output [default=OFF]" OFF)
mark_as_advanced(ENABLE_TRACE_OPTION)
# check input for this option
if(ENABLE_TRACE_OPTION STREQUAL ON)
    set(ENABLE_TRACE_OPTION ON)
else()
    set(ENABLE_TRACE_OPTION OFF)
endif()
message(STATUS "support for extended trace output is [${ENABLE_TRACE_OPTION}]")
message(STATUS "to enable/disable support for extended trace output add -DENABLE_TRACE_OPTION={ON|OFF}")

################################################################################
### option for gnu profiler
################################################################################

option(ENABLE_PROFILE "add -pg for profiling [default=OFF]" OFF)
mark_as_advanced(ENABLE_PROFILE)

# check input for this option
if(ENABLE_PROFILE STREQUAL ON)
  check_c_compiler_flag(-pg PGNUC)
  check_cxx_compiler_flag(-pg PGNUCXX)
  if(PGNUC AND PGNUCXX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
    set(LDFLAGS "${LDFLAGS} -pg")
    set(ENABLE_PROFILE ON)
  else()
    set(ENABLE_PROFILE OFF)
    message(WARNING "no support gnu -pg profiling!")
  endif()
else()
  set(ENABLE_PROFILE OFF)
endif()
message(STATUS "gnu profiler is [${ENABLE_PROFILE}]")
message(STATUS "to enable/disable -pg for profiling add -DENABLE_PROFILE={ON|OFF}")

################################################################################
### enable profiler
################################################################################

#check needed function
check_function_exists(getrusage HAVE_GETRUSAGE)

option(ENABLE_TIME_PROFILER "enable time profiling support [default=OFF]" OFF)
mark_as_advanced(ENABLE_TIME_PROFILER)

# check input for this option
if(ENABLE_TIME_PROFILER STREQUAL ON AND HAVE_GETRUSAGE)
    set(ENABLE_TIME_PROFILER ON)
else()
    set(ENABLE_TIME_PROFILER OFF)
endif()

message(STATUS "time profiling enabled is [${ENABLE_TIME_PROFILER}]")
message(STATUS "to enable/disable time profiling support add -DENABLE_TIME_PROFILER={ON|OFF}")

################################################################################
### enable select
################################################################################

# check header poll.h
check_include_file(poll.h HAVE_POLL_H)

option(ENABLE_SELECT "force the use of select instead of poll [default=OFF]" OFF)
mark_as_advanced(ENABLE_SELECT)

# check input for this option
if(ENABLE_SELECT STREQUAL ON AND HAVE_POLL_H)
    set(ENABLE_SELECT ON)
else()
    set(ENABLE_SELECT OFF)
endif()

message(STATUS "force use of select is [${ENABLE_SELECT}]")
message(STATUS "to enable/disable force the use of select instead of poll add -DENABLE_SELECT={ON|OFF}")

################################################################################
### enable pipe communication between scheduler and dispatcher
################################################################################

option(ENABLE_PIPE_COMM "use pipes to communication between scheduler and dispatcher [default=OFF]" OFF)
mark_as_advanced(ENABLE_PIPE_COMM)

if(ENABLE_PIPE_COMM STREQUAL ON)
    set(ENABLE_PIPE_COMM ON)
else()
    set(ENABLE_PIPE_COMM OFF)
endif()

message(STATUS "use pipes is [${ENABLE_PIPE_COMM}]")
message(STATUS "to enable/disable use of pipes add -DENABLE_PIPE_COMM={ON|OFF}")

################################################################################
### checks for system characteristics
################################################################################

# check BIGENDIAN
test_big_endian(WORDS_BIGENDIAN)
IF (WORDS_BIGENDIAN)
    ADD_DEFINITIONS("-DPALO_BIG_ENDIAN")
ENDIF (WORDS_BIGENDIAN)

# TODO: FIXIT test for DIR_SEPARATOR_CHAR missing
set(SEP_CHAR "/")
set(DIR_SEPARATOR_CHAR "${SEP_CHAR}")
set(DIR_SEPARATOR_STR "${SEP_CHAR}")

################################################################################
### checks for header files
################################################################################

# check stdc headers
CHECK_HEADER_STDC()
check_include_files(signal.h HAVE_SIGNAL_H)
check_include_files(string.h HAVE_STRING_H)
check_include_files(strings.h HAVE_STRINGS_H)
check_include_files(memory.h HAVE_MEMORY_H)
check_include_files(stdlib.h HAVE_STDLIB_H)
check_include_files(sys/time.h HAVE_SYS_TIME_H)
check_include_files(regex.h HAVE_REGEX_H)

check_include_files(sys/types.h HAVE_SYS_TYPES_H)
if(NOT HAVE_SYS_TYPES_H)
    set(SIZE_T "unsigned int")
endif()

# check headers
check_include_files(dlfcn.h HAVE_DLFCN_H)
check_include_files(inttypes.h HAVE_INTTYPES_H)

check_include_files(stdint.h HAVE_STDINT_H)
check_include_files(sys/stat.h HAVE_SYS_STAT_H)

check_include_files(unistd.h HAVE_UNISTD_H)

################################################################################
### checks for typedefs, structures, and compiler characteristics
################################################################################

check_type_size(suseconds_t HAVE_SUSECONDS_T)
check_type_size(size_t SIZEOF_SIZE_T)
check_type_size(double SIZEOF_DOUBLE)
check_type_size(void* SIZEOF_VOIDP)

# TODO: fix this alignof tests
check_type_size(double ALIGNOF_DOUBLE)
check_type_size(void* ALIGNOF_VOIDP)

# TODO: check if this correct
# check overloading functions for size_t and uint32_t/uint64_t

check_cxx_source_compiles(
    "#include <stdint.h>
     #include <stddef.h>
     void emil (size_t a) {}
     void emil (uint32_t a) {}
     void emil (uint64_t a) {}"
    OVERLOAD_FUNCS_SIZE_T)
if(OVERLOAD_FUNCS_SIZE_T)
    message(STATUS "overloading functions for size_t and uint32_t/uint64_t is possible")
endif()

# overloading functions for size_t and long int
check_cxx_source_compiles(
    "#include <stdint.h>
     #include <stddef.h>
     void emil (size_t a) {}
     void emil (long int a) {}
     emil (1);"
    OVERLOAD_FUNCS_SIZE_T_LONG)
if(OVERLOAD_FUNCS_SIZE_T_LONG)
    message(STATUS "overloading functions for size_t and long int is possible")
endif()

################################################################################
### check whether CC expects -export-dynamic
################################################################################

# TODO: fix check for -export-dynamic
check_library_exists(dl dlopen "" HAVE_DLOPEN)

if(HAVE_DLOPEN)
    set(INCLUDES ${INCLUDES} ${dl_INCLUDE_DIRS})
    set(LIBS ${LIBS} dl)
    link_directories(${dl_LIBRARY_DIRS})
else(HAVE_DLOPEN)
    # use -export-dynamic
endif(HAVE_DLOPEN)

################################################################################
### checks for libraries
################################################################################

# TODO: this is obsolete
# socket
check_library_exists(socket socket "/lib;/usr/lib;/usr/local/lib;/usr/pkg/lib" HAVE_SOCKETLIB)
# nsl
check_library_exists(nsl gethostbyname "/lib;/usr/lib;/usr/local/lib;/usr/pkg/lib" HAVE_NSLLIB)

################################################################################
### checks for pthreads
################################################################################

# TODO: fix pthreads
find_package(Threads)
message(STATUS "threads lib  ${CMAKE_THREAD_LIBS_INIT}")
if(CMAKE_USE_PTHREADS_INIT)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
else(CMAKE_USE_PTHREADS_INIT)
    message(FATAL_ERROR "please install the pthread library")
endif(CMAKE_USE_PTHREADS_INIT)

################################################################################
### set project information
################################################################################

# palo patch changed automatically
if(ARCH_HINT STREQUAL i386)
    set(PALO_PATCH 2)
    set(HTTPS_PATCH ${PALO_PATCH})
    set(ARCH i386)
else(ARCH_HINT STREQUAL i386)
    set(PALO_PATCH 4)
    set(HTTPS_PATCH ${PALO_PATCH})
    set(ARCH amd64)
endif(ARCH_HINT STREQUAL i386)

# set package
set(PACKAGE "palo")
set(PACKAGE_BUGREPORT "bugs@jedox.com")
set(PACKAGE_NAME "Jedox Palo")
set(PACKAGE_VERSION "${PALO_MAJOR}.${PALO_MINOR}.${PALO_PATCH}")
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME "${PACKAGE}")
set(PACKAGE_URL "www.jedox.com")

################################################################################
### find subversion client to set right build number
################################################################################

file(WRITE ${CMAKE_BINARY_DIR}/olap_revision.txt ${REVISION})

################################################################################
### update version and revison out of source
################################################################################

configure_file(${PROJECT_SOURCE_DIR}/Config/build.h.in.cmake ${CMAKE_BINARY_DIR}/Config/build.h)

################################################################################
### prepare includes and source directories
################################################################################

# if bison found generate Library/Parser/RuleParser.hpp and Library/Parser/RuleParser.cpp
if(ENABLE_USE_BISON)
    if(BISON_FOUND)
        message(STATUS "Use bison version ${BISON_VERSION} to genarate RuleParser ...")
        execute_process(COMMAND ${BISON_EXECUTABLE} -l -d -ra -S lalr1.cc -o ${PROJECT_SOURCE_DIR}/Library/Parser/RuleParser.cpp ${PROJECT_SOURCE_DIR}/Library/Parser/RuleParser.yy RESULT_VARIABLE BISON_OK ERROR_QUIET)
        set(BISON_RuleParser_OK FALSE)
        if(BISON_OK STREQUAL 0)
            set(BISON_RuleParser_OK TRUE)
        endif(BISON_OK STREQUAL 0)
        message(STATUS "the bison macro run successfully: ${BISON_RuleParser_OK}")
    endif(BISON_FOUND)
endif(ENABLE_USE_BISON)

# if flex found generate Library/Parser/RuleScanner.cpp
if(ENABLE_USE_FLEX)
    if(FLEX_FOUND)
        message(STATUS "Use flex version ${FLEX_VERSION} to generate RuleScanner ...")
        execute_process(COMMAND ${FLEX_EXECUTABLE} --nounistd -L -o ${PROJECT_SOURCE_DIR}/Library/Parser/RuleScanner.cpp ${PROJECT_SOURCE_DIR}/Library/Parser/RuleScanner.ll RESULT_VARIABLE FLEX_OK ERROR_QUIET)
        set(FLEX_RuleScanner_OK FALSE)
        if(FLEX_OK STREQUAL 0)
            set(FLEX_RuleScanner_OK TRUE)
        endif(FLEX_OK STREQUAL 0)
        message(STATUS "the flex macro run successfully: ${FLEX_RuleScanner_OK}")
    endif(FLEX_FOUND)
endif(ENABLE_USE_FLEX)

# if grep found genarate Library/PaloHttpServer/PaloCommands.h
if(ENABLE_USE_GPERF)
    if(GPERF_FOUND)
        message(STATUS "Use gperf to generate PaloCommands ...")
        execute_process(COMMAND ${GPERF_FOUND} -CGD -N PaloValue -K option -L C++ -t ${PROJECT_SOURCE_DIR}/Library/PaloHttpServer/PaloCommands.gperf OUTPUT_FILE ${PROJECT_SOURCE_DIR}/Library/PaloHttpServer/PaloCommands.h RESULT_VARIABLE GPERF_OK ERROR_QUIET)
        set(GPERF_PaloCommands_OK FALSE)
        if(GPERF_OK STREQUAL 0)
            set(GPERF_PaloCommands_OK TRUE)
        endif(GPERF_OK STREQUAL 0)
        message(STATUS "the gperf macro run successfully: ${GPERF_PaloCommands_DEFINED}")
    endif(GPERF_FOUND)
endif(ENABLE_USE_GPERF)

# create config.h file
configure_file(${PROJECT_SOURCE_DIR}/Config/config.h.in.cmake ${CMAKE_BINARY_DIR}/Config/config.h)

# add includes
include_directories(${INCLUDES})
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/Library)
include_directories(${CMAKE_BINARY_DIR}/Config)

# set directory list for palo library
set(libpalo_DIR_LIST
    Library/Collections
    Library/Dispatcher
    Library/DumpHandler
    Library/Engine
    Library/Exceptions
    Library/HttpServer
    Library/InputOutput
    Library/InputOutput/zip
    Library/Lists
    Library/Logger
    Library/Olap
    Library/Options
    Library/PaloBrowser
    Library/PaloDispatcher
    Library/PaloDocumentation
    Library/PaloHttpServer
    Library/PaloJobs
    Library/Parser
    Library/Scheduler
    Library/System
    Library/Thread
    Library/Timer
    Library/VirtualMachine
    Library/Worker
    Programs
)

set(libpalo_DIR_LIST
	${libpalo_DIR_LIST}
    Library/Network
    Library/Network/HttpClient
    Library/Network/TcpDevice
)

CollectFilesForLib("${libpalo_DIR_LIST}" LIBPALO_FILES)

set(FILE_LIST_TO_EXCLUDE "")
# need only for windows build in polo.exe
file(GLOB_RECURSE EXCLUDE_FILES Programs/palorun.cpp)
# paloexport / paloimport are separate executables, not part of palo
file(GLOB PALOEXPORT_LIBEXCLUDE Programs/paloexport/*.cpp)
file(GLOB PALOIMPORT_LIBEXCLUDE Programs/paloimport/*.cpp)
set(FILE_LIST_TO_EXCLUDE ${FILE_LIST_TO_EXCLUDE} ${EXCLUDE_FILES} ${PALOEXPORT_LIBEXCLUDE} ${PALOIMPORT_LIBEXCLUDE})
list(REMOVE_ITEM LIBPALO_FILES ${FILE_LIST_TO_EXCLUDE})

# https interface
if(ENABLE_HTTPS STREQUAL Builtin)
    # add built-in https files
    set(LIBPALO_FILES ${LIBPALO_FILES}
        Library/HttpsServer/HttpsServer.cpp
        Library/HttpsServer/HttpsServerTask.cpp)
else(ENABLE_HTTPS STREQUAL Builtin)
    if(ENABLE_HTTPS STREQUAL Module)
        set(module_DIR_LIST Library/HttpsServer)
        CollectFilesForLib("${module_DIR_LIST}" MODULE_FILES)
    endif(ENABLE_HTTPS STREQUAL Module)
endif(ENABLE_HTTPS STREQUAL Builtin)

if(ENABLE_HTTPS STREQUAL Module)
    add_custom_target(clean.https.palo COMMAND rm -f ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libhttps.palo.so.${PALO_MAJOR}.${PALO_MINOR}.${HTTPS_PATCH} VERBATIM)
    add_library(https.palo MODULE ${MODULE_FILES})
    target_link_libraries(https.palo ${OPENSSL_LIBS})
    if(SSL_LIBRARY)
        set_target_properties(https.palo PROPERTIES LINK_FLAGS -Wl,-Bsymbolic VERSION ${PALO_MAJOR}.${PALO_MINOR}.${HTTPS_PATCH} SOVERSION 0)
    else(SSL_LIBRARY)
        set_target_properties(https.palo PROPERTIES VERSION ${PALO_MAJOR}.${PALO_MINOR}.${HTTPS_PATCH} SOVERSION 0)
    endif(SSL_LIBRARY)

    if(PROJECT_BUILD_TYPE STREQUAL RelWithDebInfo)
        add_dependencies(https.palo clean.https.palo)
        add_custom_target(debug.https.palo ALL COMMAND objcopy --only-keep-debug libhttps.palo.so.${PALO_MAJOR}.${PALO_MINOR}.${HTTPS_PATCH} libhttps.palo.dbg
                                                COMMAND objcopy --strip-debug --strip-unneeded libhttps.palo.so.${PALO_MAJOR}.${PALO_MINOR}.${HTTPS_PATCH}
                                                COMMAND objcopy --add-gnu-debuglink=libhttps.palo.dbg libhttps.palo.so.${PALO_MAJOR}.${PALO_MINOR}.${HTTPS_PATCH}
                                                WORKING_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} VERBATIM)
        add_dependencies(debug.https.palo https.palo)
    endif(PROJECT_BUILD_TYPE STREQUAL RelWithDebInfo)
endif(ENABLE_HTTPS STREQUAL Module)

add_custom_target(clean.${PACKAGE} COMMAND rm -f ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${PACKAGE} VERBATIM)
add_executable(${PACKAGE} ${LIBPALO_FILES})
target_link_libraries(${PACKAGE} ${LIBS} stdc++)

if(PROJECT_BUILD_TYPE STREQUAL RelWithDebInfo)
    add_dependencies(${PACKAGE} clean.${PACKAGE})
    add_custom_target(debug.${PACKAGE} ALL COMMAND objcopy --only-keep-debug ${PACKAGE} ${PACKAGE}.dbg
                                            COMMAND objcopy --strip-debug --strip-unneeded ${PACKAGE}
                                            COMMAND objcopy --add-gnu-debuglink=${PACKAGE}.dbg ${PACKAGE}
                                            WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} VERBATIM)
    add_dependencies(debug.${PACKAGE} ${PACKAGE})
endif(PROJECT_BUILD_TYPE STREQUAL RelWithDebInfo)

################################################################################
### paloexport — export JSON structure d'un cube (client HTTP(S) sockets + OpenSSL)
################################################################################

option(ENABLE_PALOEXPORT "Build paloexport export utility" ON)
if(ENABLE_PALOEXPORT)
    add_executable(paloexport
        Programs/paloexport/paloexport_main.cpp
        Programs/paloexport/paloexport_client.cpp
    )
    target_include_directories(paloexport PRIVATE ${CMAKE_SOURCE_DIR}/Programs/paloexport)
    target_link_libraries(paloexport PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    set_target_properties(paloexport PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    message(STATUS "paloexport: enabled")
endif()

################################################################################
### paloimport — import JSON structure (nlohmann_json ; client HTTP paloexport à l’étape --execute)
################################################################################

option(ENABLE_PALOIMPORT "Build paloimport import utility" ON)
if(ENABLE_PALOIMPORT)
    find_package(nlohmann_json 3.7.0 QUIET CONFIG)
    if(NOT nlohmann_json_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3
            GIT_SHALLOW TRUE
        )
        set(JSON_BuildTests OFF CACHE INTERNAL "")
        set(JSON_Install OFF CACHE INTERNAL "")
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
    add_executable(paloimport
        Programs/paloimport/paloimport_main.cpp
        Programs/paloexport/paloexport_client.cpp
    )
    target_include_directories(paloimport PRIVATE
        ${CMAKE_SOURCE_DIR}/Programs/paloimport
        ${CMAKE_SOURCE_DIR}/Programs/paloexport
    )
    target_link_libraries(paloimport PRIVATE nlohmann_json::nlohmann_json OpenSSL::SSL OpenSSL::Crypto)
    set_target_properties(paloimport PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    message(STATUS "paloimport: enabled")
endif()

################################################################################
### enable doxygen to create documentation
################################################################################

find_package(Doxygen)

if(DOXYGEN_FOUND)
    configure_file(${CMAKE_SOURCE_DIR}/Doxyfile.in.cmake ${CMAKE_BINARY_DIR}/doc/Doxyfile @ONLY)
    add_custom_target(doc ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/doc/Doxyfile
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen" VERBATIM)
endif(DOXYGEN_FOUND)

################################################################################
### summary
################################################################################

message(STATUS "==================== Summary =========================")
message(STATUS "Configuring: Current version is ${PACKAGE_VERSION}")
message(STATUS "Configuring: Current revison is ${REVISION}")
message(STATUS "Configuring: Current target system is ${CMAKE_SYSTEM}-${ARCH_HINT}")
message(STATUS "Configuring: Extended trace output is ${ENABLE_TRACE_OPTION}")

if(ENABLE_HTTPS STREQUAL OFF)
    message(STATUS "Configuring: https interface is disabled")
else()
    if(ENABLE_HTTPS STREQUAL Module)
        message(STATUS "Configuring: https interface is compiled as shared library")
    else()
        message(STATUS "Configuring: https interface is compiled as built-in")
    endif()
endif()

message(STATUS "Configuring: Current project build type is: ${PROJECT_BUILD_TYPE}")
if(BISON_RuleParser_OK)
    message(STATUS "Configuring: BISON is available: ${BISON_EXECUTABLE}")
else()
    message(STATUS "Configuring: need a version of bison - changes to the .yy files will have no effect")
endif()

if(FLEX_RuleScanner_OK)
    message(STATUS "Configuring: LEX is available: ${FLEX_EXECUTABLE}")
else()
    message(STATUS "Configuring: need a version of flex - changes to the .ll files will have no effect")
endif()

if(GPERF_PaloCommands_OK)
    message(STATUS "Configuring: GPERF is available: ${GPERF_FOUND}")
else()
    message(STATUS "Configuring: need a version of gperf - changes to the .gperf files will have no effect")
endif()

if(ENABLE_TIME_PROFILER STREQUAL ON)
    message(STATUS "Configuring: time profiler is enabled")
else()
    message(STATUS "Configuring: time profiler is disabled")
endif()
if(PROJECT_BUILD_TYPE STREQUAL Release)
    message(STATUS "Configuring: C_FLAGS: ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
    message(STATUS "Configuring: CXX_FLAGS: ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")
else(PROJECT_BUILD_TYPE STREQUAL Release)
    if(PROJECT_BUILD_TYPE STREQUAL RelWithDebInfo)
        message(STATUS "Configuring: C_FLAGS: ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELWITHDEBINFO}")
        message(STATUS "Configuring: CXX_FLAGS: ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    else(PROJECT_BUILD_TYPE STREQUAL RelWithDebInfo)
        message(STATUS "Configuring: C_FLAGS: ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG}")
        message(STATUS "Configuring: CXX_FLAGS: ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG}")
    endif(PROJECT_BUILD_TYPE STREQUAL RelWithDebInfo)
endif(PROJECT_BUILD_TYPE STREQUAL Release)

if(GPU_ENGINE STREQUAL ON)
    message(STATUS "Configuring: NVCC_FLAGS: ${CUDA_NVCC_FLAGS}")
endif()

message(STATUS "Configuring: LIBS: ${LIBS}")
message(STATUS "Configuring: OPENSSL_LIBS: ${OPENSSL_LIBS}")

message(STATUS "Configuring: verbose make files is: ${CMAKE_VERBOSE_MAKEFILE}")
message(STATUS "======================================================")
