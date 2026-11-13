// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Caleb Kemere, Rice University

#include "LaserHatOutput.h"

#include <PluginInfo.h>
#include <string>

#ifdef _WIN32
    #include <Windows.h>
    #define EXPORT __declspec (dllexport)
#else
    #define EXPORT __attribute__ ((visibility ("default")))
#endif

using namespace Plugin;

#define NUM_PLUGINS 1

extern "C" EXPORT void getLibInfo (Plugin::LibraryInfo* info)
{
    info->apiVersion = PLUGIN_API_VER;
    info->name = "GLANCE Output";
    info->libVersion = ProjectInfo::versionString;
    info->numPlugins = NUM_PLUGINS;
}

extern "C" EXPORT int getPluginInfo (int index, Plugin::PluginInfo* info)
{
    switch (index)
    {
        case 0:
            info->type = Plugin::PROCESSOR;
            info->processor.name = "LaserHAT Output";
            info->processor.type = Plugin::Processor::SINK;
            info->processor.creator = &(Plugin::createProcessor<LaserHatOutput>);
            break;

        default:
            return -1;
    }

    return 0;
}

#ifdef _WIN32
BOOL WINAPI DllMain (IN HINSTANCE hDllHandle,
                     IN DWORD nReason,
                     IN LPVOID Reserved)
{
    return TRUE;
}
#endif
