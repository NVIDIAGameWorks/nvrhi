/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

#pragma once

#include <nvrhi/nvrhi.h>
#include <unordered_map>
#include <functional>
#include <deque>
#include <set>
#include <unordered_map>
#include <filesystem>

namespace nvrhi
{
    typedef std::pair<bool, std::reference_wrapper<const std::string>> ResolvedMarker;
    typedef std::pair<const void*, size_t> BinaryBlob;
    typedef std::function<uint64_t(BinaryBlob, nvrhi::GraphicsAPI)> ShaderHashGeneratorFunction;
    typedef std::function<BinaryBlob(uint64_t, ShaderHashGeneratorFunction)> ShaderBinaryLookupCallback;

    // Aftermath will return the payload of the last marker the GPU executed, so in cases of nested regimes,
    // we want the marker payloads to represent the whole "stack" of regimes, not just the last one
    // AftermathMarkerTracker pushes/pops regimes to this stack
    // The payload itself is a 64bit value, so AftermathMarkerTracker stores the mappings of strings<->hashes
    // There should be one AftermathMarkerTracker per graphics API-level command list
    class AftermathMarkerTracker
    {
    public:
        AftermathMarkerTracker();

        size_t pushEvent(const char* name);
        void popEvent();

        ResolvedMarker getEventString(size_t hash);
    private:
        // using a filesystem path to track the event stack since that automatically inserts "/" separators
        // and is easy to push/pop entries
        std::filesystem::path m_EventStack;
        
        // Some apps have unique marker text on every frame (for example, by appending the frame number to the marker)
        // In these cases, we want to cap the max number of strings stored to prevent memory usage from growing
        const static size_t MaxEventStrings = 128;
        std::array<size_t, MaxEventStrings> m_EventHashes;
        size_t m_OldestHashIndex;
        std::unordered_map<size_t, std::string> m_EventStrings;
    };

    // AftermathCrashDumpHelper tracks all nvrhi::IDevice-level constructs that we need when generating a crash dump
    // It provides two services: resolving a marker hash to the original string, and getting the specific shader bytecode
    // of a requested shader hash
    // There should be one AftermathCrashDumpHelper per nvrhi::IDevice
    // All command lists will register their AftermathMarkerTrackers with the AftermathCrashDumpHelper
    // Any shader bytecode loading and management code (e.g. donut's ShaderFactory) should register a shader binary lookup callback
    class AftermathCrashDumpHelper
    {
    public:
        AftermathCrashDumpHelper();
        
        void registerAftermathMarkerTracker(AftermathMarkerTracker* tracker);
        void unRegisterAftermathMarkerTracker(AftermathMarkerTracker* tracker);
        void registerShaderBinaryLookupCallback(void* client, ShaderBinaryLookupCallback lookupCallback);
        void unRegisterShaderBinaryLookupCallback(void* client);

        ResolvedMarker ResolveMarker(size_t markerHash);
        BinaryBlob findShaderBinary(uint64_t shaderHash, ShaderHashGeneratorFunction hashGenerator);
    private:
        std::set<AftermathMarkerTracker*> m_MarkerTrackers;
        // Command lists that are deleted on the CPU-side could still be executing (and crashing) GPU side,
        // so we keep around a small number of recently destroyed marker trackers just in case
        std::deque<AftermathMarkerTracker> m_DestroyedMarkerTrackers;
        std::unordered_map<void*, ShaderBinaryLookupCallback> m_ShaderBinaryLookupCallbacks;
    };
} // namespace nvrhi