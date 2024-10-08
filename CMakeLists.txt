cmake_minimum_required(VERSION 3.24.2)

project(NosOpenGLAppSample)

set(CMAKE_CXX_STANDARD 23)

set(EXTERNAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External)

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
    execute_process(COMMAND ${NODOS_PATH} --workspace "${NODOS_WORKSPACE_DIR}" sdk-info "16.0.0" process OUTPUT_VARIABLE NODOS_SDK_INFO)
    string(JSON NODOS_SDK_DIR GET ${NODOS_SDK_INFO} "path")
    string(STRIP ${NODOS_SDK_DIR} NODOS_SDK_DIR)
    set(NODOS_SDK_DIR ${NODOS_SDK_DIR} CACHE PATH "Path to Nodos SDK")
    # Make path cross-platform
    string(REPLACE "\\" "/" NODOS_SDK_DIR ${NODOS_SDK_DIR})
else()
    if (NOT DEFINED NODOS_SDK_DIR)
        message(FATAL_ERROR "NODOS_SDK_DIR or NODOS_WORKSPACE_DIR is not defined")
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH ${NODOS_SDK_DIR}/cmake)
add_library(nosAppSDK INTERFACE)
target_include_directories(nosAppSDK INTERFACE ${NODOS_SDK_DIR}/include)

add_executable(${PROJECT_NAME} ${SOURCES})
set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")

target_compile_definitions(${PROJECT_NAME} PRIVATE NODOS_APP_SDK_DLL="${NODOS_SDK_DIR}/bin/nosAppSDK.dll")
target_link_libraries(${PROJECT_NAME} PRIVATE glfw nosAppSDK glad)
