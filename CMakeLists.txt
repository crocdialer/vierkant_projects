cmake_minimum_required(VERSION 3.8)

## define our project
project(vierkant_projects)

## request C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall -Wformat=0")

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

# vierkant
option(BUILD_SHARED_LIBS "Build Shared Libraries" OFF)
option(BUILD_TESTS "Build Tests" OFF)
add_subdirectory("vierkant")
set(LIBS ${LIBS} ${vierkant_LIBRARIES})

# cereal
include_directories("cereal/include")

# projects
add_subdirectory("projects")
