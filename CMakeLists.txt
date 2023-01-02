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

message(STATUS "Including glm...")
add_subdirectory(vendor/glm EXCLUDE_FROM_ALL)

message(STATUS "Including glfw...")
option(GLFW_BUILD_DOCS OFF)
option(GLFW_BUILD_EXAMPLES OFF)
option(GLFW_BUILD_TESTS OFF)
add_subdirectory(vendor/glfw EXCLUDE_FROM_ALL)

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
set(PROJECT_SOURCES
    src/main.cpp)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES} ${IMGUI_SRCS})
target_compile_definitions(${PROJECT_NAME} PUBLIC
        GLFW_INCLUDE_NONE
        IMGUI_IMPL_VULKAN_NO_PROTOTYPES
        ImTextureID=ImU64)
target_include_directories(${PROJECT_NAME} PUBLIC
        vendor/glm
        vendor/glfw/include
        vendor/glad/include
        vendor/imgui/backends vendor/imgui
        vendor/spdlog/include)
target_link_libraries(${PROJECT_NAME}
        glfw
        glm
        spdlog)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()
