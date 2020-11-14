# add our samples(s)
FILE(GLOB FOLDER_CONTENT *)

include_directories(${vierkant_INCLUDE_DIRS})

#shaders
set(GLSLANG_VALIDATOR "glslangValidator")

set(LIBS ${LIBS})

FOREACH(folderItem ${FOLDER_CONTENT})
    IF(IS_DIRECTORY ${folderItem})

        STRING(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" itemName ${folderItem})

        if(NOT(${itemName} STREQUAL ".git"))
            MESSAGE("adding sample: '${itemName}'")

            # include optional CMakeLists.txt in samples folder
            FILE(GLOB CMAKELISTS_EXIST "${folderItem}/CMakeLists.txt")

            if(CMAKELISTS_EXIST)
                message("using CMakeLists.txt for sample '${itemName}'")
                add_subdirectory(${folderItem})

            else(CMAKELISTS_EXIST)

                set(SHADER_TARGET ${itemName}_shaders)
                set(SHADERS FALSE)

                if(IS_DIRECTORY "${folderItem}/shaders")
                    set(SHADERS TRUE)
                    FILE(GLOB SHADER_DIR "${folderItem}/shaders")
                    stringify_shaders(${SHADER_DIR} ${SHADER_TARGET} ${GLSLANG_VALIDATOR})
                    include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
                endif()

                FILE(GLOB FOLDER_SOURCES ${folderItem}/*.c* ${CMAKE_CURRENT_BINARY_DIR}/src/${SHADER_TARGET}.cpp)
                FILE(GLOB FOLDER_HEADERS ${folderItem}/*.h* ${CMAKE_BINARY_DIR}/include/${PROJECT_NAME})

                add_executable(${itemName} ${FOLDER_SOURCES} ${FOLDER_HEADERS})

                if(SHADERS)
                    add_dependencies(${itemName} ${SHADER_TARGET})
                endif()

                target_link_libraries(${itemName} ${LIBS})
            endif()
        endif()
    ENDIF()
ENDFOREACH(folderItem)
