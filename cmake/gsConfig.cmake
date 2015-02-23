######################################################################
## CMakeLists.txt ---
## This file is part of the G+Smo library. 
##
## Author: Angelos Mantzaflaris 
## Copyright (C) 2012 - 2015 RICAM-Linz.
######################################################################

## #################################################################
## Configuration
## #################################################################

## #################################################################
## Setup build types
## #################################################################

SET( CMAKE_CXX_FLAGS_MAINTAINER "-Wall -Wabi -DUSE_GISMO_STACK_WALKER"
     CACHE STRING
    "Flags used by the C++ compiler during maintainer builds."
    FORCE )
SET( CMAKE_C_FLAGS_MAINTAINER "-Wall -pedantic" CACHE STRING
    "Flags used by the C compiler during maintainer builds."
    FORCE )
SET( CMAKE_EXE_LINKER_FLAGS_MAINTAINER
    "-Wl,--warn-unresolved-symbols,--warn-once" CACHE STRING
    "Flags used for linking binaries during maintainer builds."
    FORCE )
SET( CMAKE_SHARED_LINKER_FLAGS_MAINTAINER
    "-Wl,--warn-unresolved-symbols,--warn-once" CACHE STRING
    "Flags used by the shared libraries linker during maintainer builds."
    FORCE )
MARK_AS_ADVANCED(
    CMAKE_CXX_FLAGS_MAINTAINER
    CMAKE_C_FLAGS_MAINTAINER
    CMAKE_EXE_LINKER_FLAGS_MAINTAINER
    CMAKE_SHARED_LINKER_FLAGS_MAINTAINER )

# Update the documentation string of CMAKE_BUILD_TYPE for GUIs
SET( CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING
    "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel Maintainer."
    FORCE )

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
   #set(CMAKE_BUILD_TYPE Debug CACHE STRING 
   set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING 
   "Type of build (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
   # Set the possible values of build type for cmake-gui
   set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
     "MinSizeRel" "RelWithDebInfo")
endif()

#Remove NDEBUG flag from RelWithDebInfo builds
if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    STRING(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
endif()

set(${PROJECT_NAME}_ARCHIVE_OUTPUT_DIRECTORY lib)
set(${PROJECT_NAME}_RUNTIME_OUTPUT_DIRECTORY bin)
set(${PROJECT_NAME}_LIBRARY_OUTPUT_DIRECTORY lib)

#Enable C++ 11
if(GISMO_BUILD_CPP11 AND UNIX)
   SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++0x")
endif()

# Print compilation time (this flag works on GCC compiler)
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftime-report")

if (GISMO_BUILD_COVERAGE AND CMAKE_COMPILER_IS_GNUCXX)
  # see http://www.cmake.org/Wiki/CTest:Coverage
  # and http://cmake.3232098.n2.nabble.com/Running-coverage-analysis-td7145452.html
  include(CodeCoverage)
  SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ftest-coverage -fprofile-arcs")
  SET(CMAKE_EXE_LINKER_FLAGS "-fprofile-arcs -ftest-coverage")
endif(GISMO_BUILD_COVERAGE AND CMAKE_COMPILER_IS_GNUCXX)

if (GISMO_WITH_OPENMP)
    find_package(OpenMP)
    if (OPENMP_FOUND)
       set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
       set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
       set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
    else(OPENMP_FOUND)
        message(STATUS "OpenMP Libraries were not found")
    endif(OPENMP_FOUND)
endif(GISMO_WITH_OPENMP)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
include( OptimizeForArchitecture )
endif("${CMAKE_BUILD_TYPE}" STREQUAL "Release")

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS_DEBUG      "/Od /bigobj /DWIN32 /EHsc /Zi /wd4566")
    set(CMAKE_CXX_FLAGS_RELEASE        "/bigobj /DWIN32 /EHsc /Zi /wd4566")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/bigobj /DWIN32 /EHsc /Zi /wd4566")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 18)
       set(CMAKE_CXX_FLAGS_DEBUG    "${CMAKE_CXX_FLAGS_DEBUG}  /FS")
       set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAG_RELEASE} /FS")
    endif()

    # Disable checked iterators
    set(CMAKE_CXX_FLAGS_DEBUG    "${CMAKE_CXX_FLAGS_DEBUG}  /D_SECURE_SCL=0")
    set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAG_RELEASE} /D_SECURE_SCL=0")

    # See http://msdn.microsoft.com/en-us/library/hh697468.aspx
    #add_definitions(-D_HAS_ITERATOR_DEBUGGING=0)
    #add_definitions(-D_SECURE_SCL=0)
    #add_definitions(-D_ITERATOR_DEBUG_LEVEL=0) #VS2012

    # disable incremental linking for executables (it doesn't help for linking with libraries)
    STRING(REPLACE "/INCREMENTAL:YES" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_DEBUG ${CMAKE_EXE_LINKER_FLAGS_DEBUG})
    STRING(REPLACE "/INCREMENTAL:YES" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO ${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO})

    if ( GISMO_BUILD_LIB )
    # /MD /MDd
         set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MD")
    endif()

    if (CMAKE_SIZEOF_VOID_P EQUAL 8) #64bit compiler 
       # Note: On 64bit-platforms, /Wp64 flag is present, causing extra warnings
       set(CMAKE_CXX_FLAGS_DEBUG    "${CMAKE_CXX_FLAGS_DEBUG}  /wd4244 /wd4267")
       set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAG_RELEASE} /wd4244 /wd4267")
       set(CMAKE_CXX_FLAGS_RELWITHDEBINFO  "${CMAKE_CXX_FLAG_RELWITHDEBINFO} /wd4244 /wd4267")
    #else() #32bit compiler has CMAKE_SIZEOF_VOID_P EQUAL 4
    endif()

endif()

if(GISMO_EXTRA_DEBUG)
  include(gsDebugExtra)
endif(GISMO_EXTRA_DEBUG)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  # Force to always compile with W4
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
  else()
    set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} /W4")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /W4")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /W4")
  endif()
elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  # Update if necessary
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long") # -Woverloaded-virtual -Wconversion -Wextra -pedantic
endif()
