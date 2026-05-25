if(NOT TARGET cereal::cereal)
    set(SKIP_PERFORMANCE_COMPARISON ON CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_SOURCE_DIR}/extern/cereal" "${CMAKE_BINARY_DIR}/extern/cereal")
endif()

add_library(vierkant_cereal INTERFACE)
add_library(vierkant_cereal::vierkant_cereal ALIAS vierkant_cereal)

target_include_directories(vierkant_cereal INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(vierkant_cereal INTERFACE
    vierkant
    cereal::cereal
)
