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

#include "Settings.h"

namespace
{
    class MenuWatcher :
        public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& Event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (Event.opening || Event.menuName != "PauseMenu"sv)
                return RE::BSEventNotifyControl::kContinue;

            const auto Pending = Settings::Reload();
            std::string PendingList;

            for (const auto Name : Pending)
                PendingList += std::format("{}{}", PendingList.empty() ? "" : ", ", Name);

            if (!PendingList.empty() && PendingList != NotifiedList)
            {
                const auto Message = std::format("Modern Weapon Behavior: restart the game to apply {}.", PendingList);
                RE::SendHUDMessage::ShowHUDMessage(Message.c_str(), "", true, true);
                REX::INFO("Settings: waiting for a restart: {}", PendingList);
            }

            NotifiedList = PendingList;

            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        std::string NotifiedList;
    };
}

namespace Settings
{
    void InstallMenuWatcher()
    {
        static MenuWatcher Watcher;

        if (const auto UI = RE::UI::GetSingleton())
        {
            UI->RegisterSink<RE::MenuOpenCloseEvent>(&Watcher);
            REX::INFO("Settings: re-read when the pause menu closes");
        }
    }
}
