cmake_minimum_required(VERSION 3.16)

## define our project
project(vierkant_projects)

## request C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE Release CACHE STRING
            "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
            FORCE)
endif(NOT CMAKE_BUILD_TYPE)

if (${CMAKE_BUILD_TYPE} IN_LIST RELEASE_BUILDS)
    list(APPEND CXX_FLAGS "-O3")
    add_definitions(-DNDEBUG)
else ()
    remove_definitions(-DNDEBUG)
endif ()

option(BUILD_SHARED_LIBS "Build Shared Libraries" OFF)
option(BUILD_TESTS "Build Tests" OFF)

# vierkant
add_subdirectory("extern/vierkant")
set(LIBS ${LIBS} ${vierkant_LIBRARIES})

## netzer
#add_subdirectory("submodules/netzer")
#set(LIBS ${LIBS} ${netzer_LIBRARIES})
#include_directories(${netzer_INCLUDE_DIRS})

# vierkant_cereal (owns its cereal + libzip deps; pulls in `zip` transitively)
add_subdirectory("vierkant_cereal")
set(LIBS ${LIBS} vierkant_cereal)

# cxxopts
include_directories("extern/cxxopts/include")

# projects
add_subdirectory("projects")
