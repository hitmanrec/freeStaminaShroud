#include "free_stamina/policy.hpp"

#include <array>
#include <iostream>

using free_stamina::Action;
using free_stamina::CombatState;
using free_stamina::stamina_cost;
using free_stamina::suppress_cost;

int main() {
    int checks = 0;
    int failures = 0;
    const auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << label << '\n';
        }
    };

    struct Case {
        Action action;
        const char* label;
        std::array<bool, 3> expected;
    };
    const std::array<CombatState, 3> states = {
        CombatState::outside_combat, CombatState::in_combat, CombatState::unknown};
    const std::array<Case, 7> cases = {{
        {Action::sprint, "sprint", {true, false, false}},
        {Action::jump, "jump", {true, false, false}},
        {Action::chop_tree, "axe gathering", {true, true, true}},
        {Action::mine_ore, "pickaxe gathering", {true, true, true}},
        {Action::attack_enemy, "enemy attacks", {false, false, false}},
        {Action::other, "other actions", {false, false, false}},
        {Action::unknown, "unknown actions", {false, false, false}},
    }};
    for (const auto& test : cases) {
        for (unsigned i = 0; i < states.size(); ++i) {
            check(suppress_cost(test.action, states[i]) == test.expected[i],
                  test.label);
            check(stamina_cost(12.5F, test.action, states[i]) ==
                      (test.expected[i] ? 0.0F : 12.5F),
                  test.label);
            check(stamina_cost(-3.0F, test.action, states[i]) == -3.0F,
                  "regeneration is preserved");
            check(stamina_cost(0.0F, test.action, states[i]) == 0.0F,
                  "zero cost is preserved");
        }
    }

    // Two independent players: a guest in combat must not inherit a host's
    // outside-combat exemption. State transitions must take effect immediately.
    auto host = CombatState::outside_combat;
    auto guest = CombatState::in_combat;
    check(stamina_cost(5.0F, Action::sprint, host) == 0.0F, "host outside combat");
    check(stamina_cost(5.0F, Action::sprint, guest) == 5.0F, "guest in combat");
    host = CombatState::in_combat;
    guest = CombatState::outside_combat;
    check(stamina_cost(5.0F, Action::sprint, host) == 5.0F, "host enters combat");
    check(stamina_cost(5.0F, Action::jump, guest) == 0.0F, "guest leaves combat");
    host = CombatState::outside_combat;
    check(stamina_cost(5.0F, Action::jump, host) == 0.0F, "host leaves combat");

    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
