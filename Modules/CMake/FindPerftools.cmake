# - Try to find Google perftools include dirs and libraries
#
# Usage of this module as follows:
#
#     find_package(Perftools)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  PERFTOOLS_ROOT            Preferred installation prefix for searching for
#                            Perftools, set this if the module has problems
#                            finding the proper installation path.
#  PERFTOOLS_INCLUDEDIR      Set this to the include directory of the Google
#                            perftools, if the module has problems finding the
#                            installation path.
#  PERFTOOLS_LIBRARYDIR      Set this to the library directory of the Google
#                            perftools if the module has problems finding the
#                            proper installation path.
#
# Variables defined by this module:
#
#  Perftools_FOUND           System has Google perftools, this means the
#                            include dir and all the libraries were found.
#  Perftools_INCLUDE_DIRS    Google perftools include directories.
#  Perftools_LIBRARIES       Link these to use the Google perftools libraries.
#
#  Perftools_TCMALLOC_LIBRARY        Path to the tcmalloc library.
#  Perftools_STACKTRACE_LIBRARY      Path to the stacktrace library.
#  Perftools_PROFILER_LIBRARY        Path to the profiler library.

# Reset helper search dirs
set(Perftools_ADDITIONAL_INCLUDE_SEARCH_DIRS)
set(Perftools_ADDITIONAL_LIBRARY_SEARCH_DIRS)

if (PERFTOOLS_ROOT)
    list(APPEND Perftools_ADDITIONAL_INCLUDE_SEARCH_DIRS
        ${PERFTOOLS_ROOT}/include
    )
    list(APPEND Perftools_ADDITIONAL_LIBRARY_SEARCH_DIRS
        ${PERFTOOLS_ROOT}/lib
        ${PERFTOOLS_ROOT}/lib64
        ${PERFTOOLS_ROOT}/lib/x86_64-linux-gnu
        ${PERFTOOLS_ROOT}/lib/aarch64-linux-gnu
    )
endif ()

if (PERFTOOLS_INCLUDEDIR)
    list(APPEND Perftools_ADDITIONAL_INCLUDE_SEARCH_DIRS
        ${PERFTOOLS_INCLUDEDIR}
    )
endif ()

if (PERFTOOLS_LIBRARYDIR)
    list(APPEND Perftools_ADDITIONAL_LIBRARY_SEARCH_DIRS
        ${PERFTOOLS_LIBRARYDIR}
    )
endif ()

if (Perftools_LIBRARIES AND Perftools_INCLUDE_DIRS)
    # In cache already.
    set(Perftools_FOUND true)
else ()
    find_path(Perftools_INCLUDE_DIRS
        NAMES
            google/heap-profiler.h
            gperftools/heap-profiler.h
        PATHS
            ${Perftools_ADDITIONAL_INCLUDE_SEARCH_DIRS}
            /usr/local/include
            /opt/local/include
            /sw/include
            /usr/include
    )

    # tcmalloc
    set(tcmalloc_names
        tcmalloc
        tcmalloc_minimal
    )
    find_library(perftools_tcmalloc_library
        NAMES
            ${tcmalloc_names}
        PATHS
            ${Perftools_ADDITIONAL_LIBRARY_SEARCH_DIRS}
            /usr/local/lib
            /usr/local/lib64
            /opt/local/lib
            /sw/lib
            /usr/lib
            /usr/lib64
            /usr/lib/x86_64-linux-gnu
            /usr/lib/aarch64-linux-gnu
    )

    if (perftools_tcmalloc_library AND Perftools_INCLUDE_DIRS)
        set(Perftools_TCMALLOC_LIBRARY ${perftools_tcmalloc_library})
        set(Perftools_LIBRARIES
            ${Perftools_LIBRARIES} ${perftools_tcmalloc_library})
        set(Perftools_FOUND true)
    else ()
        set(Perftools_FOUND false)
    endif ()

    # stacktrace
    set(stacktrace_names
        stacktrace
    )
    find_library(perftools_stacktrace_library
        NAMES
            ${stacktrace_names}
        PATHS
            ${Perftools_ADDITIONAL_LIBRARY_SEARCH_DIRS}
            /usr/local/lib
            /usr/local/lib64
            /opt/local/lib
            /sw/lib
            /usr/lib
            /usr/lib64
            /usr/lib/x86_64-linux-gnu
            /usr/lib/aarch64-linux-gnu
    )

    if (perftools_stacktrace_library AND Perftools_INCLUDE_DIRS)
        set(Perftools_STACKTRACE_LIBRARY ${perftools_stacktrace_library})
        set(Perftools_LIBRARIES
            ${Perftools_LIBRARIES} ${perftools_stacktrace_library})
    endif ()

    # profiler
    set(profiler_names
        profiler
    )
    find_library(perftools_profiler_library
        NAMES
            ${profiler_names}
        PATHS
            ${Perftools_ADDITIONAL_LIBRARY_SEARCH_DIRS}
            /usr/local/lib
            /usr/local/lib64
            /opt/local/lib
            /sw/lib
            /usr/lib
            /usr/lib64
            /usr/lib/x86_64-linux-gnu
            /usr/lib/aarch64-linux-gnu
    )

    if (perftools_profiler_library AND Perftools_INCLUDE_DIRS)
        set(Perftools_PROFILER_LIBRARY ${perftools_profiler_library})
        set(Perftools_LIBRARIES
            ${Perftools_LIBRARIES} ${perftools_profiler_library})
    endif ()

    if (Perftools_FOUND)
        if (NOT Perftools_FIND_QUIETLY)
            message(STATUS "Found Google perftools")
            message(STATUS "  Perftools includes: ${Perftools_INCLUDE_DIRS}")
            message(STATUS "  Perftools libraries: ${Perftools_LIBRARIES}")
        endif ()
    else ()
        if (Perftools_FIND_REQUIRED)
            message(FATAL_ERROR "Could not find Google perftools")
        endif ()
    endif ()

    mark_as_advanced(
        Perftools_INCLUDE_DIRS
        Perftools_LIBRARIES
        Perftools_TCMALLOC_LIBRARY
        Perftools_STACKTRACE_LIBRARY
        Perftools_PROFILER_LIBRARY
    )
endif ()
