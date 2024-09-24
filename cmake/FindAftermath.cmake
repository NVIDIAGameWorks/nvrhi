#
# Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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


if (NOT AFTERMATH_SEARCH_PATHS)
    set (AFTERMATH_SEARCH_PATHS
        "${CMAKE_SOURCE_DIR}/aftermath"
        "${CMAKE_PROJECT_DIR}/aftermath")
endif()

if (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        find_library(AFTERMATH_LIBRARY GFSDK_Aftermath_Lib.x64
            PATHS ${AFTERMATH_SEARCH_PATHS}
            PATH_SUFFIXES "lib/x64")
        find_file(AFTERMATH_RUNTIME_LIBRARY GFSDK_Aftermath_Lib.x64.dll
            PATHS ${AFTERMATH_SEARCH_PATHS}
            PATH_SUFFIXES "lib/x64")
    else()
        find_library(AFTERMATH_LIBRARY GFSDK_Aftermath_Lib.x86
            PATHS ${AFTERMATH_SEARCH_PATHS}
            PATH_SUFFIXES "lib/x86")
        find_library(AFTERMATH_RUNTIME_LIBRARY GFSDK_Aftermath_Lib.x86.dll
            PATHS ${AFTERMATH_SEARCH_PATHS}
            PATH_SUFFIXES "lib/x86")
    endif()
else()
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        find_library(AFTERMATH_RUNTIME_LIBRARY libGFSDK_Aftermath_Lib.x64.so
            PATHS ${AFTERMATH_SEARCH_PATHS}
            PATH_SUFFIXES "lib/x64")
    else()
        find_library(AFTERMATH_RUNTIME_LIBRARY libGFSDK_Aftermath_Lib.x86.so
            PATHS ${AFTERMATH_SEARCH_PATHS}
            PATH_SUFFIXES "lib/x86")
    endif()
endif()

find_path(AFTERMATH_INCLUDE_DIR GFSDK_Aftermath.h
    PATHS ${AFTERMATH_SEARCH_PATHS}
    PATH_SUFFIXES "include")

include(FindPackageHandleStandardArgs)

if (WIN32)
    find_package_handle_standard_args(Aftermath
        REQUIRED_VARS
            AFTERMATH_INCLUDE_DIR
            AFTERMATH_LIBRARY
            AFTERMATH_RUNTIME_LIBRARY
    )
else()
    find_package_handle_standard_args(Aftermath
        REQUIRED_VARS
            AFTERMATH_INCLUDE_DIR
            AFTERMATH_RUNTIME_LIBRARY
    )
endif()

