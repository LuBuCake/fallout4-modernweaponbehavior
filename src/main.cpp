/*
    This file is part of Modern Weapon Behavior.
    Modern Weapon Behavior is free software: you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.
    Modern Weapon Behavior is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied
    warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Modern Weapon Behavior. If not, see <https://www.gnu.org/licenses/>.
*/

#include "Core/Core.h"

namespace
{
    constexpr auto OGRuntime = REL::Version{ 1, 10, 163, 0 };
}

void MessageCallback(F4SE::MessagingInterface::Message* F4SEMessage)
{
    switch (F4SEMessage->type)
    {
        case F4SE::MessagingInterface::kGameDataReady:
            Core::GameDataReady();
            break;
        case F4SE::MessagingInterface::kPreLoadGame:
            Core::PreLoadGame();
            break;
        case F4SE::MessagingInterface::kPostLoadGame:
            Core::PostLoadGame();
            break;
        default:
            break;
    }
}

F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* F4SEInterface, F4SE::PluginInfo* Info)
{
    if (!F4SEInterface || !Info)
        return false;

    if (F4SEInterface->RuntimeVersion() != OGRuntime)
        return false;

    const auto VersionData = F4SE::PluginVersionData::GetSingleton();

    Info->infoVersion = F4SE::PluginInfo::kVersion;
    Info->name = VersionData ? VersionData->GetPluginName().data() : "Modern Weapon Behavior";
    Info->version = VersionData ? VersionData->GetPluginVersion().pack() : 0;

    return true;
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* F4SEInterface)
{
    F4SE::Init(F4SEInterface, { .logName = "MWB", .trampoline = true, .trampolineSize = 16 * 16 });

    if (!Core::RuntimeSupported())
        return true;

    Core::F4SELoaded();

    if (!F4SE::GetMessagingInterface()->RegisterListener(MessageCallback))
    {
        REX::WARN("Cannot register listener!");
        return false;
    }

    return true;
}
