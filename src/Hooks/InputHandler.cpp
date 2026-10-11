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

#include "InputHandler.h"
#include "Melee/HoldBlock.h"
#include "Reload/LoopReload.h"
#include "Reload/SightedReload.h"
#include "Weapon/ScopeSway.h"

namespace
{
    class StopRequestHandler :
        public RE::PlayerInputHandler
    {
    public:
        explicit StopRequestHandler(RE::PlayerControlsData& Data) :
            PlayerInputHandler(Data)
        {
        }

        bool ShouldHandleEvent(const RE::InputEvent* Event) override
        {
            return Event && *Event->eventType == RE::INPUT_EVENT_TYPE::kButton;
        }

        void OnButtonEvent(const RE::ButtonEvent* Event) override
        {
            if (!Event)
                return;

            const auto& UserEvent = Event->QUserEvent();

            if (UserEvent == "SecondaryAttack"sv)
            {
                SightedReload::SetAimButton(Event->value != 0.0f);
                HoldBlock::SetBlockButton(Event->value != 0.0f);
            }

            ScopeSway::OnButton(*Event);

            if (!Event->QJustPressed())
                return;

            if (UserEvent == "PrimaryAttack"sv || UserEvent == "ReadyWeapon"sv)
                Reload::RequestStop();
        }
    };
}

namespace Hooks
{
    void InstallInputHandler()
    {
        const auto Controls = RE::PlayerControls::GetSingleton();

        if (!Controls)
        {
            REX::ERROR("PlayerControls not available; stopping a loop early is disabled.");
            return;
        }

        Controls->handlers.insert(Controls->handlers.begin(), new StopRequestHandler(Controls->data));
        REX::INFO("Input handler installed");
    }
}
