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

if(TVECTORBLUR_NUKE_ROOT)
    if(EXISTS "${TVECTORBLUR_NUKE_ROOT}/cmake/NukeConfig.cmake")
        list(PREPEND CMAKE_PREFIX_PATH "${TVECTORBLUR_NUKE_ROOT}/cmake")
    elseif(EXISTS "${TVECTORBLUR_NUKE_ROOT}/NukeConfig.cmake")
        list(PREPEND CMAKE_PREFIX_PATH "${TVECTORBLUR_NUKE_ROOT}")
    else()
        message(FATAL_ERROR "No NukeConfig.cmake was found under '${TVECTORBLUR_NUKE_ROOT}'.")
    endif()
endif()

find_package(Nuke CONFIG REQUIRED)
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

function(tvectorblur_detect_platform out_os out_arch)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_os "windows")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_os "linux")
    else()
        message(FATAL_ERROR "Unsupported platform '${CMAKE_SYSTEM_NAME}'.")
    endif()

    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _processor)
    if(_processor MATCHES "^(amd64|x86_64|x64)$")
        set(_arch "x86_64")
    elseif(_processor MATCHES "^(arm64|aarch64)$")
        set(_arch "aarch64")
    else()
        message(FATAL_ERROR "Unsupported architecture '${CMAKE_SYSTEM_PROCESSOR}'.")
    endif()

    set(${out_os} "${_os}" PARENT_SCOPE)
    set(${out_arch} "${_arch}" PARENT_SCOPE)
endfunction()

tvectorblur_detect_nuke_version(TVECTORBLUR_NUKE_VERSION)
tvectorblur_detect_platform(TVECTORBLUR_PLATFORM TVECTORBLUR_ARCH)

if(TVECTORBLUR_NUKE_ROOT AND TVECTORBLUR_NUKE_VERSION VERSION_LESS "14.0")
    set_target_properties(Nuke::NDK PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${TVECTORBLUR_NUKE_ROOT}/include"
        INTERFACE_LINK_LIBRARIES
            "OpenGL::GL;${TVECTORBLUR_NUKE_ROOT}/DDImage.lib;${TVECTORBLUR_NUKE_ROOT}/RIPFramework.lib;${TVECTORBLUR_NUKE_ROOT}/glew32.lib;${TVECTORBLUR_NUKE_ROOT}/tbb.lib;${TVECTORBLUR_NUKE_ROOT}/tbbmalloc.lib"
    )
endif()

set(TVECTORBLUR_OUTPUT_DIR
    "${CMAKE_SOURCE_DIR}/bin/${TVECTORBLUR_NUKE_VERSION}/${TVECTORBLUR_PLATFORM}/${TVECTORBLUR_ARCH}")

message(STATUS "TVectorBlur will be built for Nuke ${TVECTORBLUR_NUKE_VERSION} -> ${TVECTORBLUR_OUTPUT_DIR}")

add_nuke_plugin(TVectorBlurCUDA
    src/TVectorBlurCUDA.cpp
    src/TVectorBlurCUDAKernel.cu
)

target_include_directories(TVectorBlurCUDA PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(TVectorBlurCUDA PRIVATE CUDA::cudart)
target_compile_features(TVectorBlurCUDA PRIVATE cxx_std_17)

if(TVECTORBLUR_ENABLE_FAST_MATH)
    target_compile_options(TVectorBlurCUDA PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>)
endif()

set_target_properties(TVectorBlurCUDA PROPERTIES
    OUTPUT_NAME "TVectorBlur"
    PREFIX ""
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${TVECTORBLUR_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${TVECTORBLUR_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${TVECTORBLUR_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${TVECTORBLUR_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${TVECTORBLUR_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${TVECTORBLUR_OUTPUT_DIR}"
)
