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

#include <nvrhi/common/aftermath.h>

namespace nvrhi
{
    AftermathMarkerTracker::AftermathMarkerTracker() :
        m_EventStack{},
        m_EventHashes{},
        m_OldestHashIndex(0),
        m_EventStrings{}
    {
    }

    size_t AftermathMarkerTracker::pushEvent(const char* name)
    {
        m_EventStack.append(name);
        std::string eventString = m_EventStack.generic_string();
        size_t hash = std::hash<std::string>{}(eventString);
        if (m_EventStrings.find(hash) == m_EventStrings.end())
        {
            m_EventStrings.erase(m_EventHashes[m_OldestHashIndex]);
            m_EventStrings[hash] = eventString;
            m_EventHashes[m_OldestHashIndex] = hash;
            m_OldestHashIndex = (m_OldestHashIndex + 1) % MaxEventStrings;
        }
        return hash;
    }

    void AftermathMarkerTracker::popEvent()
    {
        m_EventStack = m_EventStack.parent_path();
    }

    const static std::string NotFoundMarkerString = "ERROR: could not resolve marker";

    std::pair<bool, std::reference_wrapper<const std::string>> AftermathMarkerTracker::getEventString(size_t hash)
    {
        auto const& found = m_EventStrings.find(hash);
        if (found != m_EventStrings.end())
        {
            return std::make_pair<bool, std::reference_wrapper<const std::string>>(true, found->second);
        }
        else
        {
            // could technically return a string literal according to the spec, but compiler complains, so using static
            return std::make_pair(false, NotFoundMarkerString);
        }
    }

    AftermathCrashDumpHelper::AftermathCrashDumpHelper():
        m_MarkerTrackers{},
        m_ShaderBinaryLookupCallbacks{}
    {
    }

    void AftermathCrashDumpHelper::registerAftermathMarkerTracker(AftermathMarkerTracker* tracker)
    {
        m_MarkerTrackers.insert(tracker);
    }

    void AftermathCrashDumpHelper::unRegisterAftermathMarkerTracker(AftermathMarkerTracker* tracker)
    {
        // it's possible that a destroyed command list's markers might still be executing on the GPU,
        // so will keep the last few of them around to search in case of a crash
        const static size_t NumDestroyedMarkerTrackers = 2;
        if (m_DestroyedMarkerTrackers.size() >= NumDestroyedMarkerTrackers)
        {
            m_DestroyedMarkerTrackers.pop_front();
        }
        // copying by value to keep the tracker contents after command list is destroyed
        m_DestroyedMarkerTrackers.push_back(*tracker);
        m_MarkerTrackers.erase(tracker);
    }

    void AftermathCrashDumpHelper::registerShaderBinaryLookupCallback(void* client, ShaderBinaryLookupCallback lookupCallback)
    {
        m_ShaderBinaryLookupCallbacks[client] = lookupCallback;
    }

    void AftermathCrashDumpHelper::unRegisterShaderBinaryLookupCallback(void* client)
    {
        m_ShaderBinaryLookupCallbacks.erase(client);
    }

    ResolvedMarker AftermathCrashDumpHelper::ResolveMarker(size_t markerHash)
    {
        for (auto markerTracker : m_MarkerTrackers)
        {
            auto [found, markerString] = markerTracker->getEventString(markerHash);
            if (found)
                return std::make_pair(found, markerString);
        }
        for (auto markerTracker : m_DestroyedMarkerTrackers)
        {
            auto [found, markerString] = markerTracker.getEventString(markerHash);
            if (found)
                return std::make_pair(found, markerString);
        }
        return std::make_pair(false, NotFoundMarkerString);
    }

    BinaryBlob AftermathCrashDumpHelper::findShaderBinary(uint64_t shaderHash, ShaderHashGeneratorFunction hashGenerator)
    {
        for (auto shaderLookupClientCallback : m_ShaderBinaryLookupCallbacks)
        {
            auto [ptr, size] = shaderLookupClientCallback.second(shaderHash, hashGenerator);
            if (size > 0)
            {
                return std::make_pair(ptr, size);
            }
        }
        return std::make_pair(nullptr, 0);
    }



} // namespace nvrhi