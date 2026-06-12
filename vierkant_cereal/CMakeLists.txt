if(NOT TARGET cereal::cereal)
    set(SKIP_PERFORMANCE_COMPARISON ON CACHE BOOL "" FORCE)
    set(JUST_INSTALL_CEREAL ON CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_SOURCE_DIR}/extern/cereal" "${CMAKE_BINARY_DIR}/extern/cereal")
endif()

if(NOT TARGET zip)
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
    add_subdirectory("${CMAKE_SOURCE_DIR}/extern/libzip" "${CMAKE_BINARY_DIR}/extern/libzip" EXCLUDE_FROM_ALL)
endif()

add_library(vierkant_cereal STATIC)
add_library(vierkant_cereal::vierkant_cereal ALIAS vierkant_cereal)

target_sources(vierkant_cereal PRIVATE
    src/vierkant_cereal.cpp
    src/ziparchive.cpp
)

target_include_directories(vierkant_cereal
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${vierkant_INCLUDE_DIRS}
)

target_link_libraries(vierkant_cereal
    PUBLIC  vierkant
            cereal::cereal
            zip
)
