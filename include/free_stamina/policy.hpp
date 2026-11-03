#pragma once

namespace free_stamina {

// Semantic actions. Tool actions are free regardless of target/combat; the
// adapter must identify a tool rather than infer it from a weapon animation.
enum class Action {
    sprint,
    ordinary_jump,
    double_jump,
    slope_jump,
    glide,
    chop_tree,
    mine_ore,
    tool_action,
    attack_enemy,
    other,
    unknown,
};

enum class CombatState { outside_combat, in_combat, unknown };

// Evaluate against this player's CURRENT combat state on every cost event.
// Unknown combat state preserves traversal costs until the adapter knows more.
constexpr bool suppress_cost(Action action, CombatState combat) noexcept {
    switch (action) {
    case Action::chop_tree:
    case Action::mine_ore:
    case Action::tool_action:
        return true;
    case Action::sprint:
    case Action::double_jump:
    case Action::slope_jump:
        return combat == CombatState::outside_combat;
    default:
        return false;
    }
}

// For a hook that supplies a positive stamina DEBIT, not a stamina delta or
// current stamina value. Leave zero, regeneration, and unrelated debits alone.
constexpr float stamina_cost(float original_cost, Action action,
                             CombatState combat) noexcept {
    return original_cost > 0.0F && suppress_cost(action, combat)
               ? 0.0F
               : original_cost;
}

} // namespace free_stamina
