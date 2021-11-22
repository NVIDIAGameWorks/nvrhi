#
# Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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


set(SRC_FILES
    shaderCompiler.cpp
    options.cpp
    options.h
    ../../src/common/shader-blob.cpp
    ../../include/nvrhi/common/containers.h
    ../../include/nvrhi/common/misc.h
    ../../include/nvrhi/common/resource.h
    ../../include/nvrhi/common/shader-blob.h
    ../../include/nvrhi/nvrhi.h
)

add_executable(shaderCompiler "${SRC_FILES}")

target_link_libraries(shaderCompiler cxxopts)
target_include_directories(shaderCompiler PRIVATE ../../include)
if(MSVC)
	target_compile_definitions(shaderCompiler PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
	target_link_libraries(shaderCompiler stdc++fs pthread)
endif()

set_target_properties(shaderCompiler PROPERTIES OUTPUT_NAME "nvrhi-scomp")

set_property(TARGET shaderCompiler PROPERTY FOLDER "Tools")

if (NVRHI_INSTALL)
    install(TARGETS shaderCompiler
        RUNTIME DESTINATION bin)
endif()