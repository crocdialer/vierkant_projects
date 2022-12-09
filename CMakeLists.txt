cmake_minimum_required(VERSION 3.8)

## define our project
project(vierkant_projects)

## request C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall")

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
add_subdirectory("submodules/vierkant")
set(LIBS ${LIBS} ${vierkant_LIBRARIES})

# cereal
include_directories("submodules/cereal/include")

# libzip
cmake_policy(SET CMP0063 NEW)
set(BUILD_TOOLS OFF)
set(BUILD_REGRESS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_DOC OFF)
#set(ENABLE_BZIP2 OFF)
#set(ENABLE_ZSTD OFF)
#set(ENABLE_COMMONCRYPTO OFF)
#set(ENABLE_GNUTLS OFF)
#set(ENABLE_MBEDTLS OFF)
#set(ENABLE_OPENSSL OFF)
#set(ENABLE_WINDOWS_CRYPTO OFF)
add_subdirectory(submodules/libzip EXCLUDE_FROM_ALL)
set(LIBS ${LIBS} zip)

# projects
add_subdirectory("projects")
