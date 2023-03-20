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

#include "options.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <list>
#include <cstdio>
#include <regex>
#include <thread>
#include <mutex>
#include <csignal>
#include <nvrhi/common/shader-blob.h>
#include <nvrhi/common/misc.h>

#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem> 
namespace fs = std::experimental::filesystem;
#else
error "Missing the <filesystem> header."
#endif

#ifdef _MSC_VER 
#define popen _popen
#define pclose _pclose
#define putenv _putenv
#endif

using namespace std;

CommandLineOptions g_Options;
string g_PlatformName;

struct CompileTask
{
	string sourceFile;
	string shaderName;
	string entryPoint;
	string combinedDefines;
	string commandLine;
};

vector<CompileTask> g_CompileTasks;
int g_OriginalTaskCount;
atomic<int> g_ProcessedTaskCount;
mutex g_TaskMutex;
mutex g_ReportMutex;
bool g_Terminate = false;
bool g_CompileSuccess = true;
fs::file_time_type g_ConfigWriteTime;

struct BlobEntry
{
	fs::path compiledPermutationFile;
	string permutation;
};

map<string, vector<BlobEntry>> g_ShaderBlobs;

map<fs::path, fs::file_time_type> g_HierarchicalUpdateTimes;
vector<fs::path> g_IgnoreIncludes;

const char* g_SharedCompilerOptions = "-nologo ";

string path_string(fs::path path)
{
	return path.make_preferred().string();
}

bool getHierarchicalUpdateTime(const fs::path& rootFilePath, list<fs::path>& callStack, fs::file_time_type& outTime)
{
	static basic_regex<char> include_pattern("\\s*#include\\s+[\"<]([^>\"]+)[>\"].*");

	auto found = g_HierarchicalUpdateTimes.find(rootFilePath);
	if (found != g_HierarchicalUpdateTimes.end())
	{
		outTime = found->second;
		return true;
	}

	ifstream inputFile(rootFilePath);
	if (!inputFile.is_open())
	{
		cout << "ERROR: Cannot open file  " << path_string(rootFilePath) << endl;
		for (const fs::path& otherPath : callStack)
			cout << "            included in  " << path_string(otherPath) << endl;

		return false;
	}

	callStack.push_front(rootFilePath);

	fs::path rootBasePath = rootFilePath.parent_path();
	fs::file_time_type hierarchicalUpdateTime = fs::last_write_time(rootFilePath);

	uint32_t lineno = 0;
	for (string line; getline(inputFile, line);)
	{
		lineno++;

		std::match_results<const char*> result;
		std::regex_match(line.c_str(), result, include_pattern);
		if (!result.empty())
		{
			fs::path include = string(result[1]);

			bool ignoreThisInclude = false;
			for (const fs::path& ignoredPath : g_IgnoreIncludes)
			{
				if (ignoredPath == include)
				{
					ignoreThisInclude = true;
					break;
				}
			}

			if (ignoreThisInclude)
				continue;

			bool foundIncludedFile = false;
			fs::path includedFilePath = rootBasePath / include;
			if (fs::exists(includedFilePath))
			{
				foundIncludedFile = true;
			}
			else
			{
				for (const string& includePath : g_Options.includePaths)
				{
					includedFilePath = includePath / include;
					if (fs::exists(includedFilePath))
					{
						foundIncludedFile = true;
						break;
					}
				}
			}

			if (!foundIncludedFile)
			{
				cout << "ERROR: Cannot find include file  " << path_string(include) << endl;
				for (const fs::path& otherPath : callStack)
					cout << "                    included in  " << path_string(otherPath) << endl;

				return false;
			}

			fs::file_time_type dependencyTime;
			if (!getHierarchicalUpdateTime(includedFilePath, callStack, dependencyTime))
				return false;

			hierarchicalUpdateTime = std::max(dependencyTime, hierarchicalUpdateTime);
		}
	}

	callStack.pop_front();

	g_HierarchicalUpdateTimes[rootFilePath] = hierarchicalUpdateTime;
	outTime = hierarchicalUpdateTime;

	return true;
}

string buildCompilerCommandLine(const CompilerOptions& options, const fs::path& shaderFile, const fs::path& outputFile)
{
	std::ostringstream ss;
#ifdef _WIN32
	ss << "%COMPILER% ";
#else
	ss << "$COMPILER ";
#endif
	ss << path_string(shaderFile) << " ";
	ss << "-Fo " << path_string(outputFile) << " ";
	ss << "-T " << options.target << " ";
	if (!options.entryPoint.empty())
		ss << "-E " << options.entryPoint << " ";
	for (const string& define : options.definitions)
		ss << "-D" << define << " ";
	for (const string& define : g_Options.additionalDefines)
		ss << "-D" << define << " ";
	for (const string& dir : g_Options.includePaths)
		ss << "-I" << path_string(dir) << " ";

	ss << g_SharedCompilerOptions;

	for (const string& option : g_Options.additionalCompilerOptions)
	{
		ss << option << " ";
	}

	if (g_Options.platform == Platform::SPIRV)
	{
		ss << "-spirv ";

		for (int space = 0; space < 10; space++)
		{
			ss << "-fvk-t-shift " << g_Options.vulkanTextureShift << " " << space << " ";
			ss << "-fvk-s-shift " << g_Options.vulkanSamplerShift << " " << space << " ";
			ss << "-fvk-b-shift " << g_Options.vulkanConstantShift << " " << space << " ";
			ss << "-fvk-u-shift " << g_Options.vulkanUavShift << " " << space << " ";
		}
	}
	
	return ss.str();
}

void printError(uint32_t lineno, const string& error)
{
	cerr << g_Options.inputFile << "(" << lineno << "): " << error << endl;
}

fs::path removeLeadingDotDots(const fs::path& path)
{
	auto it = path.begin();

	while (*it == ".." && it != path.end())
		++it;

	fs::path result;

	while (it != path.end())
	{
		result = result / *it;
		++it;
	}

	return result;
}

bool processShaderConfig(uint32_t lineno, const string& shaderConfig)
{
	CompilerOptions compilerOptions;
	if (!compilerOptions.parse(shaderConfig))
	{
		printError(lineno, compilerOptions.errorMessage);
		return false;
	}
	
	ostringstream combinedDefines;
	for (const string& define : compilerOptions.definitions)
	{
		combinedDefines << define << " ";
	}

	uint32_t permutationHash = nvrhi::hash_to_u32(std::hash<std::string>()(combinedDefines.str()));

	fs::path compiledShaderName;
	if (compilerOptions.outputPath.empty())
	{
		compiledShaderName = removeLeadingDotDots(compilerOptions.shaderName);
		compiledShaderName.replace_extension("");
		if (!compilerOptions.entryPoint.empty() && compilerOptions.entryPoint != "main")
			compiledShaderName += "_" + compilerOptions.entryPoint;
		compiledShaderName += ".bin";
	}
	else
	{
		compiledShaderName = compilerOptions.outputPath;
	}

	fs::path sourceFile = fs::path(g_Options.inputFile).parent_path() / compilerOptions.shaderName;

	fs::path compiledShaderPath = g_Options.outputPath / compiledShaderName.parent_path();
	if (!fs::exists(compiledShaderPath))
	{
		cout << "INFO: Creating directory " << compiledShaderPath << endl;
		fs::create_directories(compiledShaderPath);
	}
	else if(!g_Options.force)
	{
		fs::path compiledShaderFile = g_Options.outputPath / compiledShaderName;
		if (fs::exists(compiledShaderFile))
		{
			fs::file_time_type compiledFileTime = fs::last_write_time(compiledShaderFile);

			fs::file_time_type sourceHierarchyTime;
			list<fs::path> callStack;
			if (!getHierarchicalUpdateTime(sourceFile, callStack, sourceHierarchyTime))
				return false;

			sourceHierarchyTime = std::max(sourceHierarchyTime, g_ConfigWriteTime);

			if (compiledFileTime > sourceHierarchyTime)
				return true;
		}
	}

	fs::path compiledPermutationName = compiledShaderName;
	compiledPermutationName.replace_extension("");
	if (compilerOptions.definitions.size() > 0)
	{
		char buf[16];
		sprintf(buf, "_%08x", permutationHash);
		compiledPermutationName += buf;
	}
	compiledPermutationName += ".bin";

	fs::path compiledPermutationFile = g_Options.outputPath / compiledPermutationName;

	string commandLine = buildCompilerCommandLine(compilerOptions, sourceFile, compiledPermutationFile);
	
	CompileTask task;
	task.sourceFile = sourceFile.generic_string();
	task.shaderName = compilerOptions.shaderName;
	task.entryPoint = compilerOptions.entryPoint;
	task.combinedDefines = combinedDefines.str();
	task.commandLine = commandLine;
	g_CompileTasks.push_back(task);

	if (!compilerOptions.definitions.empty())
	{
		BlobEntry entry;
		entry.compiledPermutationFile = compiledPermutationFile;
		entry.permutation = combinedDefines.str();

		vector<BlobEntry>& entries = g_ShaderBlobs[path_string(compiledShaderName)];
		entries.push_back(entry);
	}

	return true;
}

bool expandPermutations(uint32_t lineno, const string& shaderConfig)
{
	size_t opening = shaderConfig.find('{');
	if (opening != string::npos)
	{
		size_t closing = shaderConfig.find('}', opening);
		if (closing == string::npos)
		{
			printError(lineno, "missing }");
			return false;
		}

		size_t current = opening + 1;
		while(true)
		{
			size_t comma = shaderConfig.find(',', current);

			if (comma == string::npos || comma > closing)
				comma = closing;

			string newConfig = shaderConfig.substr(0, opening) 
				+ shaderConfig.substr(current, comma - current) 
				+ shaderConfig.substr(closing + 1);

			if (!expandPermutations(lineno, newConfig))
				return false;

			current = comma + 1;

			if(comma >= closing)
				break;
		}

		return true;
	}

	return processShaderConfig(lineno, shaderConfig);
}

// a version of std::isspace that is a bit more compatible between various compilers
inline bool _isspace(int ch)
{
	return strchr(" \t\r\n", ch) != nullptr;
}

bool trim(string& s)
{
	size_t pos;
	pos = s.find('#');
	if (pos != string::npos)
		s.erase(pos, s.size() - pos);

	// remove leading whitespace
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
		return !_isspace(ch);
	}));

	// remove trailing whitespace
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
		return !_isspace(ch);
	}).base(), s.end());

	return !s.empty();
}

bool WriteShaderBlob(const string& compiledShaderName, const vector<BlobEntry>& entries)
{
	fs::path outputFilePath = fs::path(g_Options.outputPath) / compiledShaderName;
	string outputFileName = path_string(outputFilePath);

	FILE* outputFile = fopen(outputFileName.c_str(), "wb");
	if (!outputFile)
	{
		cout << "ERROR: cannot write " << outputFileName << endl;
		return false;
	}

	if (g_Options.verbose)
	{
		cout << "INFO: writing " << outputFileName << endl;
	}

	fwrite("NVSP", 1, 4, outputFile);

	for (const BlobEntry& entry : entries)
	{
		string inputFileName = path_string(entry.compiledPermutationFile);
		FILE* inputFile = fopen(inputFileName.c_str(), "rb");

		if (!inputFile)
		{
			cout << "ERROR: cannot read " << inputFileName << endl;
			fclose(outputFile);
			return false;
		}

		fseek(inputFile, 0, SEEK_END);
		size_t fileSize = ftell(inputFile);
		fseek(inputFile, 0, SEEK_SET);

		if (fileSize == 0)
		{
			fclose(inputFile);
			continue;
		}

		if (fileSize > size_t(std::numeric_limits<uint32_t>::max()))
		{
			cout << "ERROR: binary shader file too big: " << inputFileName << endl;
			fclose(inputFile);
			continue;
		}

		void* buffer = malloc(fileSize);
		fread(buffer, 1, fileSize, inputFile);
		fclose(inputFile);

		if (!g_Options.keep)
		{
			fs::remove(inputFileName);
		}
		
		nvrhi::ShaderBlobEntry binaryEntry;
		binaryEntry.permutationSize = (uint32_t)entry.permutation.size();
		binaryEntry.dataSize = (uint32_t)fileSize;

		fwrite(&binaryEntry, 1, sizeof(binaryEntry), outputFile);
		fwrite(entry.permutation.data(), 1, entry.permutation.size(), outputFile);
		fwrite(buffer, 1, fileSize, outputFile);
	}

	fclose(outputFile);

	return true;
}

void compileThreadProc()
{
	while (!g_Terminate)
	{
		CompileTask task;
		{
			lock_guard<mutex> guard(g_TaskMutex);
			if (g_CompileTasks.empty())
				return;

			task = g_CompileTasks[g_CompileTasks.size() - 1];
			g_CompileTasks.pop_back();
		}

		if (g_Options.verbose)
		{
			lock_guard<mutex> guard(g_ReportMutex);
			cout << task.commandLine << endl;
		}

		string commandLine = task.commandLine + " 2>&1";

		FILE* pipe = popen(commandLine.c_str(), "r");
		if (!pipe)
		{
			lock_guard<mutex> guard(g_ReportMutex);
			cout << "ERROR: cannot run " << g_Options.compilerPath << endl;
			g_CompileSuccess = false;
			g_Terminate = true;
			return;
		}

		ostringstream ss;
		char buf[1024];
		while (fgets(buf, sizeof(buf), pipe))
			ss << buf;

		int result = pclose(pipe);
		g_ProcessedTaskCount++;

		{
			lock_guard<mutex> guard(g_ReportMutex);

			const char* resultCode = (result == 0) ? " OK  " : "FAIL ";
			float progress = (float)g_ProcessedTaskCount / (float)g_OriginalTaskCount;

			sprintf(buf, "[%5.1f%%] %s %s %s:%s %s", 
				progress * 100.f, 
				g_PlatformName.c_str(), 
				resultCode, 
				task.shaderName.c_str(), 
				task.entryPoint.c_str(), 
				task.combinedDefines.c_str());

			cout << buf << endl;
 
			if (result != 0 && !g_Terminate)
			{
				cout << "ERRORS for " << task.shaderName << ":" << task.entryPoint << " " << task.combinedDefines << ": " << endl;
				cout << ss.str() << endl;
				g_CompileSuccess = false;
			}
		}
	}
}

void signal_handler(int sig)
{
	(void)sig;

	g_Terminate = true;

	lock_guard<mutex> guard(g_ReportMutex);
	cout << "SIGINT received, terminating" << endl;
}

int main(int argc, char** argv)
{
	if (!g_Options.parse(argc, argv))
	{
		cout << g_Options.errorMessage << endl;
		return 1;
	}

	switch (g_Options.platform)
	{
	case Platform::DXBC: g_PlatformName = "DXBC"; break;
	case Platform::DXIL: g_PlatformName = "DXIL"; break;
	case Platform::SPIRV: g_PlatformName = "SPIR-V"; break;
	case Platform::UNKNOWN: g_PlatformName = "UNKNOWN"; break; // never happens
	}

	for (const auto& fileName : g_Options.ignoreFileNames)
	{
		g_IgnoreIncludes.push_back(fileName);
	}
	
	g_ConfigWriteTime = fs::last_write_time(g_Options.inputFile);

	// Updated shaderCompiler executable also means everything must be recompiled
	g_ConfigWriteTime = std::max(g_ConfigWriteTime, fs::last_write_time(argv[0]));
	
	ifstream configFile(g_Options.inputFile);
	uint32_t lineno = 0;
	for(string line; getline(configFile, line);)
	{
		lineno++;

		if (!trim(line))
			continue;

		if (!expandPermutations(lineno, line))
			return 1;
	}

	if (g_CompileTasks.empty())
	{
		cout << "All " << g_PlatformName << " outputs are up to date." << endl;
		return 0;
	}

	g_OriginalTaskCount = (int)g_CompileTasks.size();
	g_ProcessedTaskCount = 0;

	{
		// Workaround for weird behavior of _popen / cmd.exe on Windows
		// with quotes around the executable name and also around some other arguments.

		static char envBuf[1024]; // use a static array because putenv uses it by reference
#ifdef WIN32
		snprintf(envBuf, sizeof(envBuf), "COMPILER=\"%s\"", g_Options.compilerPath.c_str());
#else
		snprintf(envBuf, sizeof(envBuf), "COMPILER=%s", g_Options.compilerPath.c_str());
#endif
		putenv(envBuf);
		
		if (g_Options.verbose)
		{
			cout << envBuf << endl;
		}
	}

	unsigned int threadCount = thread::hardware_concurrency();
	if (threadCount == 0 || !g_Options.parallel)
	{
		threadCount = 1;
	}

	signal(SIGINT, signal_handler);

	vector<thread> threads;
	threads.resize(threadCount);
	for (unsigned int threadIndex = 0; threadIndex < threadCount; threadIndex++)
	{
		threads[threadIndex] = thread(compileThreadProc);
	}
	for (unsigned int threadIndex = 0; threadIndex < threadCount; threadIndex++)
	{
		threads[threadIndex].join();
	}

	if (!g_CompileSuccess || g_Terminate)
		return 1;

	for (const pair<const string, vector<BlobEntry>>& it : g_ShaderBlobs)
	{
		if (!WriteShaderBlob(it.first, it.second))
			return 1;
	}

	return 0;
}
