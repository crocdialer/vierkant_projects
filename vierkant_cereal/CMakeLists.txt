if(NOT TARGET cereal::cereal)
    set(SKIP_PERFORMANCE_COMPARISON ON CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_SOURCE_DIR}/extern/cereal" "${CMAKE_BINARY_DIR}/extern/cereal")
endif()

add_library(vierkant_cereal STATIC)
add_library(vierkant_cereal::vierkant_cereal ALIAS vierkant_cereal)

target_sources(vierkant_cereal PRIVATE src/vierkant_cereal.cpp)

target_include_directories(vierkant_cereal
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${vierkant_INCLUDE_DIRS}
)

target_link_libraries(vierkant_cereal
    PUBLIC  vierkant
            cereal::cereal
)
