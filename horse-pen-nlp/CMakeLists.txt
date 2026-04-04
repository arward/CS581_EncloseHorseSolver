cmake_minimum_required(VERSION 3.20)
project(horse_pen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# Gurobi discovery
# Priority: -DGUROBI_HOME=... > $GUROBI_HOME env > common prefixes
# --------------------------------------------------------------------------
if(NOT DEFINED GUROBI_HOME)
    if(DEFINED ENV{GUROBI_HOME})
        set(GUROBI_HOME "$ENV{GUROBI_HOME}")
        message(STATUS "Gurobi: using GUROBI_HOME env = ${GUROBI_HOME}")
    else()
        file(GLOB _gurobi_candidates
            "C:/gurobi*/win64"
            "/opt/gurobi*/linux64"
            "$ENV{HOME}/gurobi*/linux64"
            "/Library/gurobi*/macos_universal2"
        )
        list(SORT _gurobi_candidates ORDER DESCENDING)
        foreach(_candidate IN LISTS _gurobi_candidates)
            if(EXISTS "${_candidate}/include/gurobi_c++.h")
                set(GUROBI_HOME "${_candidate}")
                break()
            endif()
        endforeach()
    endif()
endif()

if(NOT GUROBI_HOME)
    message(FATAL_ERROR
        "Could not find Gurobi. Set GUROBI_HOME:\n"
        "  cmake -DGUROBI_HOME=C:/gurobi1301/win64 ..\n"
        "or export GUROBI_HOME before running cmake.")
endif()
message(STATUS "Gurobi home: ${GUROBI_HOME}")

# Header
find_path(GUROBI_INCLUDE_DIR
    NAMES gurobi_c++.h
    PATHS "${GUROBI_HOME}/include"
    NO_DEFAULT_PATH
)
if(NOT GUROBI_INCLUDE_DIR)
    message(FATAL_ERROR "gurobi_c++.h not found under ${GUROBI_HOME}/include")
endif()

# C++ wrapper library.
# Windows ships several variants depending on runtime linkage:
#   gurobi_c++md2017  = /MD  release  <-- correct for Release builds
#   gurobi_c++mdd2017 = /MDd debug
#   gurobi_c++mt2017  = /MT  release (static CRT)
#   gurobi_c++mtd2017 = /MTd debug   (static CRT)
# Linux/macOS: libgurobi_c++.a
if(WIN32)
    find_library(GUROBI_CXX_LIBRARY
        NAMES gurobi_c++md2017 gurobi_c++mt2017 gurobi_c++
        PATHS "${GUROBI_HOME}/lib"
        NO_DEFAULT_PATH
    )
else()
    find_library(GUROBI_CXX_LIBRARY
        NAMES gurobi_c++
        PATHS "${GUROBI_HOME}/lib"
        NO_DEFAULT_PATH
    )
endif()

# Core library - version number is baked into the name (e.g. gurobi130.lib)
file(GLOB _gurobi_core_candidates
    "${GUROBI_HOME}/lib/gurobi[0-9]*.lib"
    "${GUROBI_HOME}/lib/libgurobi[0-9]*.so"
    "${GUROBI_HOME}/lib/libgurobi[0-9]*.dylib"
)
list(SORT _gurobi_core_candidates ORDER DESCENDING)
# Use the glob result directly - find_library chokes on full paths as NAMES
list(GET _gurobi_core_candidates 0 GUROBI_LIBRARY)

if(NOT GUROBI_CXX_LIBRARY OR NOT GUROBI_LIBRARY)
    message(FATAL_ERROR
        "Gurobi libraries not found in ${GUROBI_HOME}/lib.\n"
        "CXX lib: ${GUROBI_CXX_LIBRARY}\n"
        "Core lib: ${GUROBI_LIBRARY}")
endif()
message(STATUS "Gurobi CXX lib : ${GUROBI_CXX_LIBRARY}")
message(STATUS "Gurobi core lib: ${GUROBI_LIBRARY}")

# Imported targets
add_library(Gurobi::GurobiCXX STATIC IMPORTED)
set_target_properties(Gurobi::GurobiCXX PROPERTIES
    IMPORTED_LOCATION "${GUROBI_CXX_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${GUROBI_INCLUDE_DIR}"
)

if(WIN32)
    add_library(Gurobi::Gurobi STATIC IMPORTED)
else()
    add_library(Gurobi::Gurobi SHARED IMPORTED)
endif()
set_target_properties(Gurobi::Gurobi PROPERTIES
    IMPORTED_LOCATION "${GUROBI_LIBRARY}"
)

# --------------------------------------------------------------------------
# libcurl
# --------------------------------------------------------------------------
find_package(CURL REQUIRED)

# --------------------------------------------------------------------------
# Executable
# --------------------------------------------------------------------------
add_executable(enclosed
    src/main.cpp
    src/grid.cpp
    src/solver.cpp
)

target_include_directories(enclosed PRIVATE include)

target_link_libraries(enclosed PRIVATE
    Gurobi::GurobiCXX
    Gurobi::Gurobi
    CURL::libcurl
)

# Gurobi C++ API requires exceptions
if(MSVC)
    target_compile_options(enclosed PRIVATE /EHsc /W3)
else()
    target_compile_options(enclosed PRIVATE -fexceptions)
endif()

# --------------------------------------------------------------------------
# Windows: copy Gurobi DLL next to the executable so it can run in-place
# --------------------------------------------------------------------------
if(WIN32)
    file(GLOB _gurobi_dlls "${GUROBI_HOME}/bin/gurobi*.dll")
    foreach(_dll IN LISTS _gurobi_dlls)
        add_custom_command(TARGET enclosed POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll}" "$<TARGET_FILE_DIR:enclosed>"
            COMMENT "Copying ${_dll}"
        )
    endforeach()
endif()

# --------------------------------------------------------------------------
# Install
# --------------------------------------------------------------------------
install(TARGETS enclosed DESTINATION bin)

# --------------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------------
message(STATUS "")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++        : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Gurobi     : ${GUROBI_HOME}")
message(STATUS "")
