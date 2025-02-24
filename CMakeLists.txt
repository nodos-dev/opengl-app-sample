cmake_minimum_required(VERSION 3.24.2)

project(NosOpenGLAppSample)

set(CMAKE_CXX_STANDARD 23)

set(EXTERNAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External)


set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
add_subdirectory(${EXTERNAL_DIR}/glfw)


# add glad
add_library(glad ${EXTERNAL_DIR}/glad/src/glad.c)
target_include_directories(glad PUBLIC ${EXTERNAL_DIR}/glad/include)

file(GLOB_RECURSE SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_SOURCE_DIR}/bin>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_SOURCE_DIR}/bin>)

if (DEFINED NODOS_PATH)
    # Use nosman to get SDK PATH
    cmake_path(SET NODOS_EFFECTIVE_PATH ${NODOS_PATH})
    get_filename_component(NODOS_WORKSPACE_DIR ${NODOS_EFFECTIVE_PATH} DIRECTORY)
    set(NOSMAN_WORKSPACE_DIR ${NODOS_WORKSPACE_DIR})
    set(NOSMAN_EXECUTABLE ${NODOS_PATH})
    execute_process(COMMAND ${NODOS_PATH} --workspace "${NODOS_WORKSPACE_DIR}" sdk-info "17.2.0" process OUTPUT_VARIABLE NODOS_SDK_INFO ERROR_VARIABLE NODOS_SDK_INFO_ERROR)
    if (NODOS_SDK_INFO_ERROR)
        message(FATAL_ERROR "Failed to get Nodos SDK Info: \n${NODOS_SDK_INFO_ERROR}")
    endif()
    string(JSON NODOS_SDK_DIR GET ${NODOS_SDK_INFO} "path")
    string(STRIP ${NODOS_SDK_DIR} NODOS_SDK_DIR)
    set(NODOS_SDK_DIR ${NODOS_SDK_DIR} CACHE PATH "Path to Nodos SDK")
    # Make path cross-platform
    string(REPLACE "\\" "/" NODOS_SDK_DIR ${NODOS_SDK_DIR})
else()
    message(FATAL_ERROR "NODOS_PATH is not defined")
endif()

include(${NODOS_WORKSPACE_DIR}/Toolchain/CMake/Scripts/Utils.cmake)
include(${NODOS_WORKSPACE_DIR}/Toolchain/CMake/Scripts/Projects.cmake)

# Get nos.sys.vulkan
nos_get_module("nos.sys.vulkan" "5.8" NOS_SYS_VULKAN_TARGET)
message(STATUS "nos.sys.vulkan target: ${NOS_SYS_VULKAN_TARGET}")

list(APPEND CMAKE_MODULE_PATH ${NODOS_SDK_DIR}/cmake)
add_library(nosAppSDK INTERFACE)
target_include_directories(nosAppSDK INTERFACE ${NODOS_SDK_DIR}/include)

add_executable(${PROJECT_NAME} ${SOURCES})
set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")

if (WIN32)
target_compile_definitions(${PROJECT_NAME} PRIVATE NODOS_APP_SDK_DLL="${NODOS_SDK_DIR}/bin/nosAppSDK.dll")
elseif (UNIX)
target_compile_definitions(${PROJECT_NAME} PRIVATE NODOS_APP_SDK_DLL="${NODOS_SDK_DIR}/bin/libnosAppSDK.so")
elseif (APPLE)
target_compile_definitions(${PROJECT_NAME} PRIVATE NODOS_APP_SDK_DLL="${NODOS_SDK_DIR}/bin/libnosAppSDK.dylib")
else ()
message(FATAL_ERROR "Unsupported platform")
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE glfw nosAppSDK glad ${NOS_SYS_VULKAN_TARGET})
