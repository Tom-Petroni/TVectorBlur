cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

project(TVectorBlur VERSION 1.0.0 LANGUAGES CXX CUDA)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TVECTORBLUR_ENABLE_FAST_MATH "Enable CUDA fast math optimizations." ON)
set(TVECTORBLUR_NUKE_ROOT "" CACHE PATH "Path to the Nuke installation root (the folder that contains NukeConfig.cmake or cmake/NukeConfig.cmake).")

if(MSVC)
    add_compile_definitions(HAVE_SNPRINTF NOMINMAX _USE_MATH_DEFINES)
endif()

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES native)
endif()

if(NOT TVECTORBLUR_NUKE_ROOT)
    message(FATAL_ERROR "TVECTORBLUR_NUKE_ROOT must point to the extracted Nuke SDK root.")
endif()

set(TVECTORBLUR_NUKE_INCLUDE_DIR "${TVECTORBLUR_NUKE_ROOT}/include")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(TVECTORBLUR_NUKE_DDIMAGE_LIB "${TVECTORBLUR_NUKE_ROOT}/DDImage.lib")
    set(TVECTORBLUR_NUKE_RIPFRAMEWORK_LIB "${TVECTORBLUR_NUKE_ROOT}/RIPFramework.lib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TVECTORBLUR_NUKE_DDIMAGE_LIB "${TVECTORBLUR_NUKE_ROOT}/libDDImage.so")
    set(TVECTORBLUR_NUKE_RIPFRAMEWORK_LIB "${TVECTORBLUR_NUKE_ROOT}/libRIPFramework.so")
else()
    message(FATAL_ERROR "Unsupported platform '${CMAKE_SYSTEM_NAME}'.")
endif()

if(NOT EXISTS "${TVECTORBLUR_NUKE_INCLUDE_DIR}")
    message(FATAL_ERROR "Nuke include directory was not found at '${TVECTORBLUR_NUKE_INCLUDE_DIR}'.")
endif()

if(NOT EXISTS "${TVECTORBLUR_NUKE_DDIMAGE_LIB}")
    message(FATAL_ERROR "DDImage library was not found at '${TVECTORBLUR_NUKE_DDIMAGE_LIB}'.")
endif()

find_package(CUDAToolkit REQUIRED)

function(tvectorblur_detect_nuke_version out_var)
    set(_version "")

    if(TVECTORBLUR_NUKE_ROOT)
        get_filename_component(_nuke_name "${TVECTORBLUR_NUKE_ROOT}" NAME)
        string(REGEX MATCH "[0-9]+\\.[0-9]+" _version "${_nuke_name}")
    endif()

    if(NOT _version AND DEFINED Nuke_VERSION AND NOT Nuke_VERSION STREQUAL "")
        set(_version "${Nuke_VERSION}")
    elseif(NOT _version AND EXISTS "${Nuke_DIR}/NukeConfigVersion.cmake")
        file(STRINGS "${Nuke_DIR}/NukeConfigVersion.cmake" _version_line
            REGEX "^set\\(PACKAGE_VERSION [0-9]+\\.[0-9]+\\.[0-9]+\\)$")
        if(_version_line)
            string(REGEX REPLACE "^set\\(PACKAGE_VERSION ([0-9]+\\.[0-9]+)\\.[0-9]+\\)$" "\\1"
                _version "${_version_line}")
        endif()
    endif()

    if(NOT _version)
        get_filename_component(_nuke_root "${Nuke_DIR}/.." ABSOLUTE)
        get_filename_component(_nuke_name "${_nuke_root}" NAME)
        string(REGEX MATCH "[0-9]+\\.[0-9]+" _version "${_nuke_name}")
    endif()

    if(NOT _version)
        message(FATAL_ERROR "Unable to detect the Nuke major.minor version from '${Nuke_DIR}'.")
    endif()

    string(REGEX MATCH "^[0-9]+\\.[0-9]+" _major_minor "${_version}")
    if(NOT _major_minor)
        message(FATAL_ERROR "Unsupported Nuke version string '${_version}'.")
    endif()

    set(${out_var} "${_major_minor}" PARENT_SCOPE)
endfunction()

function(tvectorblur_detect_platform out_os)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_os "windows")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_os "linux")
    else()
        message(FATAL_ERROR "Unsupported platform '${CMAKE_SYSTEM_NAME}'.")
    endif()

    set(${out_os} "${_os}" PARENT_SCOPE)
endfunction()

tvectorblur_detect_nuke_version(TVECTORBLUR_NUKE_VERSION)
tvectorblur_detect_platform(TVECTORBLUR_PLATFORM)

set(TVECTORBLUR_ARCH "x86_64")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(TVECTORBLUR_BINARY_PREFIX "")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TVECTORBLUR_BINARY_PREFIX "lib")
else()
    message(FATAL_ERROR "Unsupported platform '${CMAKE_SYSTEM_NAME}'.")
endif()

set(TVECTORBLUR_OUTPUT_DIR
    "${CMAKE_SOURCE_DIR}/TVectorBlur/bin/${TVECTORBLUR_NUKE_VERSION}/${TVECTORBLUR_PLATFORM}/${TVECTORBLUR_ARCH}")

message(STATUS "TVectorBlur will be built for Nuke ${TVECTORBLUR_NUKE_VERSION} -> ${TVECTORBLUR_OUTPUT_DIR}")

add_library(TVectorBlurCUDA MODULE
    src/TVectorBlurCUDA.cpp
    src/TVectorBlurCUDAKernel.cu
)

target_include_directories(
    TVectorBlurCUDA
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${TVECTORBLUR_NUKE_INCLUDE_DIR}"
)
target_link_libraries(TVectorBlurCUDA PRIVATE CUDA::cudart "${TVECTORBLUR_NUKE_DDIMAGE_LIB}")
if(EXISTS "${TVECTORBLUR_NUKE_RIPFRAMEWORK_LIB}")
    target_link_libraries(TVectorBlurCUDA PRIVATE "${TVECTORBLUR_NUKE_RIPFRAMEWORK_LIB}")
endif()
target_compile_features(TVectorBlurCUDA PRIVATE cxx_std_17)

if(TVECTORBLUR_ENABLE_FAST_MATH)
    target_compile_options(TVectorBlurCUDA PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>)
endif()

set_target_properties(TVectorBlurCUDA PROPERTIES
    OUTPUT_NAME "TVectorBlur"
    PREFIX "${TVECTORBLUR_BINARY_PREFIX}"
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${TVECTORBLUR_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${TVECTORBLUR_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${TVECTORBLUR_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${TVECTORBLUR_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${TVECTORBLUR_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${TVECTORBLUR_OUTPUT_DIR}"
)
