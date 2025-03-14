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
add_subdirectory("submodules/vierkant")
set(LIBS ${LIBS} ${vierkant_LIBRARIES})

## netzer
#add_subdirectory("submodules/netzer")
#set(LIBS ${LIBS} ${netzer_LIBRARIES})
#include_directories(${netzer_INCLUDE_DIRS})

# cereal
include_directories("submodules/cereal/include")

# cxxopts
include_directories("submodules/cxxopts/include")

# libzip
set(BUILD_TOOLS OFF)
set(BUILD_REGRESS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_DOC OFF)
set(ENABLE_BZIP2 OFF)
#set(ENABLE_COMMONCRYPTO OFF)
set(ENABLE_GNUTLS OFF)
set(ENABLE_MBEDTLS OFF)
set(ENABLE_OPENSSL OFF)
set(ENABLE_WINDOWS_CRYPTO OFF)
set(ENABLE_ZSTD ON)
set(CMAKE_POLICY_DEFAULT_CMP0063 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(submodules/libzip EXCLUDE_FROM_ALL)
set(LIBS ${LIBS} zip)

# projects
add_subdirectory("projects")
