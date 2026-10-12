Modern Weapon Behavior
======================

F4SE plugin of **Modern Weapon Behavior (MWB)**, an all-in-one overhaul of Fallout 4's weapon behaviors for first and third person.

This repository holds the source code of the plugin (`MWB.dll`) as released on Nexus Mods. The behavior files, animations, scripts and interface files ship with the mod itself.

## Features

* Looped reload the way Bethesda does it in Fallout 76 and Starfield: one shell per pass, blended passes, cancel at the end of the pass.
* Tactical reload, sighted reload, sprint transitions and sprinting equips, chosen per weapon through JSON files, by weapon, keyword or form list.
* Inspect, ammo check (with an on-screen magazine readout), special attack and fire mode switch, callable from Papyrus and MCM hotkeys.
* Arm-only animations with weapon lock, slide lock and per fire mode weapon bone poses.
* Animation annotations to fire ammo, use items and cast spells from any animation.
* Weapon HUD module, bullet in the chamber, drop unspent ammo on reload and manual reload.
* Hold to block for melee and fists, and firing while sprinting in first person.
* Aim sway and hold breath, the game's own, for regular scopes, 3D scopes and sights without a scope, each with its own setting.

## Supported Runtimes

One DLL for every runtime:

* Old-Gen `1.10.163`
* Next-Gen `1.10.980` and `1.10.984`
* Anniversary Edition `1.11.137` to `1.11.240`

## Pre-requisites

To play:

* [Fallout 4 Script Extender (F4SE)](https://f4se.silverlock.org/)
* [Address Library for F4SE Plugins](https://www.nexusmods.com/fallout4/mods/47327)

To build:

* [Visual Studio 2022 or newer](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload (MSVC and a Windows 10/11 SDK)
* [XMake](https://xmake.io) 3.0.0 or newer
* [Git](https://git-scm.com/)

## Building

```bat
git clone --recurse-submodules https://github.com/LuBuCake/fallout4-modernweaponbehavior
cd fallout4-modernweaponbehavior
xmake f -y -p windows -a x64 -m releasedbg
xmake build
```

The plugin is written to `build/windows/x64/releasedbg/MWB.dll`, with its `MWB.pdb` next to it.

[CommonLibF4](https://github.com/Dear-Modding-FO4/commonlibf4) comes as a git submodule in `lib/commonlibf4`, pinned to the exact commit the release was built with: [`2fc2bf95`](https://github.com/Dear-Modding-FO4/commonlibf4/tree/2fc2bf95e264f946d67102b2eef6286fad6d9f36). If you cloned without `--recurse-submodules`, run `git submodule update --init --recursive`.

### Visual Studio

```bat
xmake project -k vsxmake
```

Generates a `vsxmake20XX/` solution that stays in sync with `xmake.lua`.

### Installing straight to the game

Set one of these environment variables and run `xmake install`:

* `XSE_FO4_MODS_PATH`: a mod manager's mods folder; the plugin goes to `<path>/MWB/F4SE/Plugins`.
* `XSE_FO4_GAME_PATH`: the game folder; the plugin goes to `<path>/Data/F4SE/Plugins`.

### Troubleshooting

* **`'_Mbstatet': 'struct' type redefinition`**: more than one Windows SDK is installed and the headers got mixed. Pin one: `xmake f -c` and then `xmake f -y -p windows -a x64 -m releasedbg --vs_sdkver=10.0.26100.0`.
* **XMake picks `mingw`**: happens when building from Git Bash. Build from PowerShell or the Developer Command Prompt, or keep `-p windows` in `xmake f`.

## Project Layout

```
src/
  main.cpp     F4SE entry points (OG query and load)
  Actions/     first person only actions and weapon actions allowed per weapon
  Arm/         arm-only animations: weapon lock and arm space
  Config/      the per weapon JSON files and the exclusions
  Core/        runtime check, F4SE messages and the equipped weapon
  Fire/        MWB_WeaponFireAmmo annotation
  HUD/         weapon HUD module and ammo check readout
  Hooks/       animation events, clip generators, graph generate, input, queued events, animation update
  Magic/       MWB_UseAlchemyItem and MWB_CastSpell annotations
  Melee/       hold to block
  Papyrus/     native functions of the MWB script
  Reload/      looped, sighted and tactical reload, bullet in the chamber, drop unspent ammo, manual reload, bolt charge resync
  Settings/    MWB.ini, MWB_Custom.ini and the MCM settings
  Sprint/      sprint transitions, third person sprint camera, first person sprint speed and firing while sprinting
  Weapon/      fire mode switch and pose, slide lock, weapon bones and scope sway
```

## License

Modern Weapon Behavior codebase is licensed under [GPL-3.0](LICENSE) WITH the [Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source)](EXCEPTIONS), the same terms as CommonLibF4.
Please refer to the LICENSE and EXCEPTIONS files for detailed information.
