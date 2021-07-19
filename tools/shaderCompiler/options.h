/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <string>
#include <vector>

enum class Platform
{
	UNKNOWN,
	DXBC,
	DXIL,
	SPIRV
};

struct CommandLineOptions
{
	std::string inputFile;
	std::string outputPath;
	std::vector<std::string> includePaths;
	std::vector<std::string> additionalDefines;
    std::vector<std::string> ignoreFileNames;
    std::vector<std::string> additionalCompilerOptions;
	std::string compilerPath;
	Platform platform = Platform::UNKNOWN;
	bool parallel = false;
	bool verbose = false;
	bool force = false;
	bool help = false;
	bool keep = false;
	int vulkanTextureShift = 0;
	int vulkanSamplerShift = 128;
	int vulkanConstantShift = 256;
	int vulkanUavShift = 384;

	std::string errorMessage;

	bool parse(int argc, char** argv);
};

struct CompilerOptions
{
	std::string shaderName;
	std::string entryPoint;
	std::string target;
	std::string outputPath;
	std::vector<std::string> definitions;

	std::string errorMessage;

	bool parse(std::string line);
};
