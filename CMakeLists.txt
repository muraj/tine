cmake_minimum_required(VERSION 3.1)

project(tine C CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 11)

message(STATUS "Retrieving external vendor code...")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

message(STATUS "Including glfw...")
option(GLFW_BUILD_DOCS OFF)
option(GLFW_BUILD_EXAMPLES OFF)
option(GLFW_BUILD_TESTS OFF)
add_subdirectory(vendor/glfw EXCLUDE_FROM_ALL)

message(STATUS "Including glslang...")
find_program(GLSL_VALIDATOR glslangValidator)
mark_as_advanced(FORCE GLSL_VALIDATOR)
if (NOT GLSL_VALIDATOR)
    message(STATUS "glslValidator not found, building from source...")
    option(ENABLE_HLSL OFF)
    add_subdirectory(vendor/glslang EXCLUDE_FROM_ALL)
    set(GLSL_VALIDATOR $<TARGET_FILE:glslangValidator>)
    set(GLSL_VALIDATOR_TARGET glslangValidator)
else()
    message(STATUS "glslValidator found: ${GLSL_VALIDATOR}")
endif()

function(glsl_compile)
    cmake_parse_arguments(PARSE_ARGV 0 GLSL_COMPILE_ARGS "" "FILE;COMPILE_FLAGS" "")
    get_filename_component(GLSL_COMPILE_ARGS_FILENAME ${GLSL_COMPILE_ARGS_FILE} ABSOLUTE)
    get_filename_component(GLSL_COMPILE_ARGS_NAME ${GLSL_COMPILE_ARGS_FILE} NAME)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${GLSL_COMPILE_ARGS_NAME}.spv
        COMMAND ${GLSL_VALIDATOR} ${GLSL_COMPILE_ARGS_COMPILE_FLAGS} -V ${GLSL_COMPILE_ARGS_FILENAME} -o ${GLSL_COMPILE_ARGS_NAME}.spv
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        MAIN_DEPENDENCY ${GLSL_COMPILE_ARGS_FILE}
        DEPENDS ${GLSL_VALIDATOR_TARGET}
        COMMENT "Compiling shader ${GLSL_COMPILE_ARGS_FILE}..."
        VERBATIM
    )
endfunction()

function(embed_binary)
    cmake_parse_arguments(PARSE_ARGV 0 EMBED_BIN_ARGS "" "FILE;TEMPLATE;VARNAME" "")
    get_filename_component(EMBED_BIN_ARGS_FILENAME ${EMBED_BIN_ARGS_FILE} ABSOLUTE)
    get_filename_component(EMBED_BIN_ARGS_TEMPLATENAME ${EMBED_BIN_ARGS_TEMPLATE} ABSOLUTE)
    get_filename_component(EMBED_BIN_ARGS_NAME ${EMBED_BIN_ARGS_FILE} NAME)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${EMBED_BIN_ARGS_NAME}.cpp
        COMMAND ${CMAKE_COMMAND} -DTEMPLATE_FILE=${EMBED_BIN_ARGS_TEMPLATENAME} -DIN_FILE=${EMBED_BIN_ARGS_FILENAME} -DOUT_FILE=${EMBED_BIN_ARGS_NAME}.cpp -DVAR_NAME=${EMBED_BIN_ARGS_VARNAME} -P ${PROJECT_SOURCE_DIR}/cmake/bin2c.cmake
        MAIN_DEPENDENCY ${EMBED_BIN_ARGS_FILE}
        DEPENDS ${PROJECT_SOURCE_DIR}/cmake/bin2c.cmake ${EMBED_BIN_ARGS_TEMPLATE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Embedding binary ${EMBED_BIN_ARGS_FILE}..."
        VERBATIM
    )
endfunction()

message(STATUS "Including VulkanMemoryAllocator...")
option(VMA_STATIC_VULKAN_FUNCTIONS "" OFF)
option(VMA_DYNAMIC_VULKAN_FUNCTIONS "" ON)
set(VMA_SRCS
    vendor/VulkanMemoryAllocator/src/VmaUsage.cpp
    vendor/VulkanMemoryAllocator/src/VmaUsage.h)

message(STATUS "Including glm...")
add_subdirectory(vendor/glm EXCLUDE_FROM_ALL)

message(STATUS "Including imgui...")
set(IMGUI_DIR vendor/imgui)
set(IMGUI_SRCS
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp)

message(STATUS "Including spdlog...")
add_subdirectory(vendor/spdlog EXCLUDE_FROM_ALL)

message(STATUS "Setting up build...")

glsl_compile(FILE src/shaders/basic_triangle.vert)
embed_binary(FILE ${CMAKE_CURRENT_BINARY_DIR}/basic_triangle.vert.spv TEMPLATE cmake/bin2c.template.in VARNAME vert_shader_code)

glsl_compile(FILE src/shaders/basic_triangle.frag)
embed_binary(FILE ${CMAKE_CURRENT_BINARY_DIR}/basic_triangle.frag.spv TEMPLATE cmake/bin2c.template.in VARNAME frag_shader_code)

set(PROJECT_SOURCES
    src/main.cpp
    src/tine_engine.cpp
    src/tine_renderer.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/basic_triangle.vert.spv.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/basic_triangle.frag.spv.cpp)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES} ${IMGUI_SRCS} ${VMA_SRCS})
target_compile_definitions(${PROJECT_NAME} PUBLIC
    NOMINMAX        
    ImTextureID=ImU64)
target_include_directories(${PROJECT_NAME} PUBLIC
        vendor/glm
        vendor/glfw/include
        vendor/glad/include
        vendor/imgui/backends vendor/imgui
        vendor/spdlog/include
        vendor/VulkanMemoryAllocator/include)
target_link_libraries(${PROJECT_NAME}
        glfw
        glm
        spdlog)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()
