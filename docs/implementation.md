# Stamina mod implementation

## Current deliverable

The portable C++17 stamina policy and its executable tests are implemented.
A separate read-only Enshwick C diagnostic DLL now logs per-player combat
candidates. See [Arch/Proton instructions](arch-proton.md) for building and
installing it. It does not apply the stamina policy or install game hooks.
Neither policy tests nor fake-host tests demonstrate live game behavior or
multiplayer replication.

Run `sh scripts/test.sh` from the repository, or invoke it by absolute path.
It needs a C++17 compiler and no downloaded dependencies. Build outputs stay
in `.build/`.

On Windows, the same tests can be built from a Visual Studio developer prompt:

```bat
if not exist .build mkdir .build
cl /nologo /std:c++17 /EHsc /W4 /WX /I include tests\policy_test.cpp /Fo.build\policy_test.obj /Fe.build\policy_test.exe
.build\policy_test.exe
```

The Windows command has not been executed in the Linux cloud environment.

## Behavior

| Action | Outside combat | In combat | Combat state unknown |
| --- | --- | --- | --- |
| Sprint | Free | Original cost | Original cost |
| Jump | Free | Original cost | Original cost |
| Cut a tree with an axe | Free | Free | Free |
| Mine ore with a pickaxe | Free | Free | Free |
| Attack an enemy, including with either tool | Original cost | Original cost | Original cost |
| Other or unidentified action | Original cost | Original cost | Original cost |

Suppress only positive stamina debits. Do not refill stamina, change maximum
stamina, modify regeneration, or grant gathering exemptions based on the held
tool alone. Punching trees and digging dirt are outside the current scope.

## Integration research required

Target: Enshrouded on Linux through Proton, with an in-game hosted world.
Reported game version is approximately `1.0.1.0`; exact executable build and
installed loader status are unconfirmed. Windows clients are also a target if
guests must install the mod.

Before choosing a loader or building a game adapter, verify:

1. The loader supports this game's engine and exact build, and can load under
   Proton as well as Windows. No Unity, BepInEx, Lua, or other API is assumed.
2. Actual action-specific stamina debit hooks and the game's combat-state source
   exist. A generic resource multiplier cannot express these rules.
3. Gathering can be distinguished from enemy attacks with the same weapon.
4. Each cost event can be attributed to its own player and current combat state.
5. Whether the host can authoritatively suppress costs for unmodified guests.
   Do not infer this from host-local changes or from dedicated-server behavior.

The adapter should pass the verified action and that player's state to
`free_stamina::stamina_cost`. It must preserve original game behavior when it
cannot identify an action or traversal combat state. No offsets, signatures,
combat timers, or loader entry points have been invented as substitutes.

## First playable milestone

Connect sprint stamina debits and the real combat state for the local player.
Test entering and leaving combat with partially depleted stamina. Then connect
jump and the two gathering actions; verify enemy attacks retain their costs.

Finally test a modded in-game host with an unmodified guest. Check both players
in different combat states. Claim host-only support only if the guest's observed
costs match all rules without correction or desynchronization. Otherwise ship
client installations for Windows and Proton, after testing both platforms.
