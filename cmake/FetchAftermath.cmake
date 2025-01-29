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

if( TARGET aftermath )
    return()
endif()

if (NOT AFTERMATH_SEARCH_PATHS)
    include(FetchContent)
    if(WIN32)
        set(AFTERMATH_SDK_URL https://developer.nvidia.com/downloads/assets/tools/secure/nsight-aftermath-sdk/2024_3_0/windows/NVIDIA_Nsight_Aftermath_SDK_2024.3.0.24312.zip)
        set(AFTERMATH_SDK_MD5 232145E8A749F873B8EE32B9DA6F09D6)
    else()
        set(AFTERMATH_SDK_URL https://developer.nvidia.com/downloads/assets/tools/secure/nsight-aftermath-sdk/2024_3_0/linux/NVIDIA_Nsight_Aftermath_SDK_2024.3.0.24312.tgz)
        set(AFTERMATH_SDK_MD5 C118C60F7A8D8302AF53513EA4FF1ABD)
    endif()

    FetchContent_Declare(
        aftermath_sdk
        URL ${AFTERMATH_SDK_URL}
        URL_HASH MD5=${AFTERMATH_SDK_MD5}
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
   set(AFTERMATH_SEARCH_PATHS "${CMAKE_BINARY_DIR}/_deps/aftermath_sdk-src/")
   FetchContent_MakeAvailable(aftermath_sdk)
endif()

find_path(AFTERMATH_INCLUDE_DIR GFSDK_Aftermath.h
    PATHS ${AFTERMATH_SEARCH_PATHS}
    PATH_SUFFIXES "include")

find_library(AFTERMATH_LIBRARY GFSDK_Aftermath_Lib.x64
    PATHS ${AFTERMATH_SEARCH_PATHS}
    REQUIRED
    PATH_SUFFIXES "lib/x64")

add_library(aftermath SHARED IMPORTED)
target_include_directories(aftermath INTERFACE ${AFTERMATH_INCLUDE_DIR})

if(WIN32)
    find_file(AFTERMATH_RUNTIME_LIBRARY GFSDK_Aftermath_Lib.x64.dll
        PATHS ${AFTERMATH_SEARCH_PATHS}
        PATH_SUFFIXES "lib/x64")
    set_property(TARGET aftermath PROPERTY IMPORTED_LOCATION ${AFTERMATH_RUNTIME_LIBRARY})
    set_property(TARGET aftermath PROPERTY IMPORTED_IMPLIB ${AFTERMATH_LIBRARY})
else()
    set_property(TARGET aftermath PROPERTY IMPORTED_LOCATION ${AFTERMATH_LIBRARY})
endif()
