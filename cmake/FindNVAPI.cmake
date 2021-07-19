#
# Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.


find_package(PackageHandleStandardArgs)

if (WIN32)

    if (NOT NVAPI_SEARCH_PATHS)
        set (NVAPI_SEARCH_PATHS
            "${CMAKE_SOURCE_DIR}/nvapi"
            "${CMAKE_PROJECT_DIR}/nvapi")
    endif()

    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        find_library(NVAPI_LIBRARY nvapi64 
            PATHS ${NVAPI_SEARCH_PATHS}
            PATH_SUFFIXES amd64)
    else()
        find_library(NVAPI_LIBRARY nvapi 
            PATHS ${NVAPI_SEARCH_PATHS}
            PATH_SUFFIXES x86)
    endif()

    find_path(NVAPI_INCLUDE_DIR nvapi.h 
        PATHS ${NVAPI_SEARCH_PATHS})
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(NVAPI
    REQUIRED_VARS
        NVAPI_INCLUDE_DIR
        NVAPI_LIBRARY
)

