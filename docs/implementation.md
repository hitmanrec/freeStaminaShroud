# Stamina mod implementation

Version 0.5.0 provides a Windows x86-64 Enshwick DLL for the two explicitly
reviewed client builds. The host sprint path has been validated in game on
build /205, including blocking on wolf aggression before the first hit.
Jump and tool suppression are new experimental gameplay changes, tested in
native fake-host harnesses and cross-compiled, but still need in-game validation.
Guest behavior with an active patch is not confirmed.

The C++17 policy in `include/free_stamina/policy.hpp` expresses the semantic
rules. The C game adapter implements the corresponding guarded decisions;
it does not link the C++ policy into the freestanding DLL.

| Action | Outside combat | In combat | Unknown combat |
| --- | --- | --- | --- |
| Sprint | Free | Original cost | Original cost |
| Ordinary jump | Already free | Original behavior | Original behavior |
| Double/slope jump | Free | Original cost | Original cost |
| Tool action, including an enemy hit or swing at air | Free | Free | Free |
| Ordinary combat weapon attack | Original cost | Original cost | Original cost |
| Glider, swimming, other/unknown action | Original cost | Original cost | Original cost |

The user simplified the tool requirement: no gathering-versus-combat split is
needed for axes/pickaxes. The runtime rule checks the live Tools item category
and TwoHanded animation at the reviewed action debit site. Other matching tools
can also qualify; the full classification limits are documented below.

- [Gameplay rules, implementation details and 0.5.0 test](action-preview.md)
- [Arch Linux / Proton build and installation](arch-proton.md)
- [Per-player guest diagnostics and test](guest-test.md)
- [Explicit build identities and code verification](build-205-support.md)
- [Validated host sprint session](session-044-success.md)

Unknown identities, unverified call sites, other attributes, stale permissions,
missing components and wrong threads preserve the game's original cost.
Debits are suppressed at their source, not refunded later. Combat permissions
remain per player. No maximum stamina or regeneration multiplier is changed.

Run all checks with `sh scripts/test-all.sh`; use `sh scripts/build-probe.sh`
with Zig 0.14.1 to cross-compile the DLL. Outputs remain in `.build/`.
These checks do not substitute for live gameplay or guest prediction testing.
