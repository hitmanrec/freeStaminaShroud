// Included after probe.c's read-only host and bounded log helpers.
#define AI_LIMIT 4096
#define AI_TRACKED 64
#define EVENT_LIMIT 64
#define EVENT_TYPES_LIMIT 2048
static uint64_t last_step;
static uint32_t event_lines, event_dropped;
static int range_ok(uint64_t offset, uint64_t width, uint64_t size) {
    return offset <= size && width <= size - offset;
}
static uint64_t little(const uint8_t *data, unsigned width) {
    uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= (uint64_t)data[i] << (8 * i);
    return value;
}
static Field resolve(const char *type, const char *field, uint32_t width, int component) {
    Field f = {type, field, width, 0, 0, 0, 0};
    EnshwickStatus s = api->type_size(str(type), &f.size);
    if (s == EnshwickStatus_OK) s = api->field_offset(str(type), str(field), &f.offset);
    if (s == EnshwickStatus_OK && component) s = api->type_hash(str(type), &f.hash);
    if (s == EnshwickStatus_OK && !range_ok(f.offset, width, f.size)) s = EnshwickStatus_BAD_ARGS;
    f.available = s == EnshwickStatus_OK;
    Line l = {{0}, 0}; text(&l, "resolve "); text(&l, type); text(&l, "."); text(&l, field);
    text(&l, " status="); number(&l, s);
    if (f.available) {
        text(&l, " offset="); number(&l, f.offset);
        text(&l, " width="); number(&l, width);
        text(&l, " type_size="); number(&l, f.size);
    }
    emit(&l); return f;
}
static void diagnostic_status(const char *what, EnshwickStatus s) {
    Line l = {{0}, 0}; text(&l, what); text(&l, " status="); number(&l, s); emit(&l);
}

// Same attribute container algorithm as Enshwick World::vitals, with additional
// range checks. Resolve constants from this build; never assume storage slots.
static struct {
    uint32_t hash, size;
    uint64_t data, flags, count, root, maximum;
    int available;
} stamina_layout;
typedef struct { uint32_t known, entity, status, current, maximum; } StaminaCache;
static StaminaCache stamina_cache[PLAYERS];
static EnshwickStatus read_stamina(EnshwickEntity entity, uint32_t *current, uint32_t *maximum) {
    if (!stamina_layout.available) return EnshwickStatus_MISSING;
    const void *pointer = 0;
    EnshwickStatus s = api->component(self, entity, stamina_layout.hash, &pointer);
    if (s != EnshwickStatus_OK) return s;
    if (!pointer) return EnshwickStatus_MISSING;
    const uint8_t *p = (const uint8_t *)pointer;
    uint64_t offset = little(p + stamina_layout.data, 2);
    uint64_t count = p[stamina_layout.count];
    if (little(p, 4) != stamina_layout.root || !(p[stamina_layout.flags] & 1))
        return EnshwickStatus_NOT_LOADED;
    if (stamina_layout.maximum >= count || !range_ok(offset, count * 4, stamina_layout.size))
        return EnshwickStatus_BAD_ARGS;
    *current = (uint32_t)little(p + offset, 4);
    *maximum = (uint32_t)little(p + offset + stamina_layout.maximum * 4, 4);
    return EnshwickStatus_OK;
}
static void signed32(Line *l, uint32_t n) {
    if (n & UINT32_C(0x80000000)) { text(l, "-"); number(l, (uint32_t)(~n + 1)); }
    else number(l, n);
}
static void sample_stamina(const EnshwickTickData *t, uint32_t slot, EnshwickEntity entity, int heartbeat) {
    if (!stamina_layout.available) return;
    uint32_t current = 0, maximum = 0;
    EnshwickStatus s = read_stamina(entity, &current, &maximum);
    StaminaCache *c = &stamina_cache[slot];
    if (heartbeat || !c->known || c->entity != entity || c->status != s ||
        c->current != current || c->maximum != maximum) {
        Line l = {{0}, 0}; prefix(&l, t); text(&l, " entity="); number(&l, entity);
        text(&l, " stamina_status="); number(&l, s);
        if (s == EnshwickStatus_OK) {
            text(&l, " current="); signed32(&l, current);
            text(&l, " maximum="); signed32(&l, maximum);
        }
        emit(&l);
    }
    *c = (StaminaCache){1, entity, s, current, maximum};
}
static void init_stamina(void) {
    stamina_layout.available = 0;
    if (!api->constant) { diagnostic_status("stamina_schema", EnshwickStatus_MISSING); return; }
    EnshwickStatus s = api->type_hash(str("keen::ecs::Stamina"), &stamina_layout.hash);
    if (s == EnshwickStatus_OK) s = api->type_size(str("keen::ecs::Stamina"), &stamina_layout.size);
    const char *names[] = {"ecs.level_data_offset", "ecs.level_flags", "ecs.attribute_count",
                           "attribute.stamina", "attribute.stamina_max_slot"};
    uint64_t *out[] = {&stamina_layout.data, &stamina_layout.flags, &stamina_layout.count,
                       &stamina_layout.root, &stamina_layout.maximum};
    for (unsigned i = 0; i < 5 && s == EnshwickStatus_OK; ++i)
        s = api->constant(self, str(names[i]), out[i]);
    if (s == EnshwickStatus_OK && (stamina_layout.size < 4 ||
        !range_ok(stamina_layout.data, 2, stamina_layout.size) ||
        !range_ok(stamina_layout.flags, 1, stamina_layout.size) ||
        !range_ok(stamina_layout.count, 1, stamina_layout.size) ||
        stamina_layout.root > UINT32_MAX || stamina_layout.maximum >= 256)) s = EnshwickStatus_BAD_ARGS;
    stamina_layout.available = s == EnshwickStatus_OK;
    diagnostic_status("stamina_schema", s);
}

static Field targets, target_entity, target_root, faction;
static uint32_t enemy_hash;
static int ai_available;
static EnshwickEntityPlace ai_entities[AI_LIMIT];
typedef struct { uint32_t entity, any, primary, seen; } AiCache;
static AiCache ai_cache[AI_TRACKED];
static uint32_t ai_previous_total = UINT32_MAX;
static void target_line(const EnshwickTickData *t, uint32_t owner, uint32_t any, uint32_t primary) {
    Line l = {{0}, 0}; prefix(&l, t); text(&l, " ai_candidate="); number(&l, owner);
    text(&l, " player_slot_mask="); number(&l, any);
    text(&l, " primary_slot_mask="); number(&l, primary);
    const void *p = 0;
    EnshwickStatus s = faction.available ? api->component(self, owner, faction.hash, &p) : EnshwickStatus_MISSING;
    text(&l, " faction_status="); number(&l, s);
    if (s == EnshwickStatus_OK && p) {
        text(&l, " faction_raw="); bytes(&l, (const uint8_t *)p + faction.offset, 1);
    }
    p = 0;
    s = enemy_hash ? api->component(self, owner, enemy_hash, &p) : EnshwickStatus_MISSING;
    text(&l, " enemy_component_status="); number(&l, s);
    emit(&l);
}
static void sample_targets(const EnshwickTickData *t, const EnshwickPlayerInfo *players,
                            uint32_t count, int heartbeat) {
    last_step = t->step;
    if (!ai_available) return;
    uint32_t total = 0, ready = 0, matched = 0, overflow = 0;
    // Enumerate the actual target-bearing runtime component, not a setup-only
    // enemy type. Do not silently lose all AI on a static/dynamic type split.
    EnshwickStatus s = api->entities(self, targets.hash, ai_entities, AI_LIMIT, &total);
    // On a failed query do not report lost targets: observation is unavailable.
    if (s != EnshwickStatus_OK) {
        if (heartbeat) diagnostic_status("ai_query", s);
        return;
    }
    for (unsigned i = 0; i < AI_TRACKED; ++i) ai_cache[i].seen = 0;
    uint32_t limit = total < AI_LIMIT ? total : AI_LIMIT;
    for (uint32_t i = 0; i < limit; ++i) {
        int owner_is_player = 0;
        for (uint32_t p = 0; p < count; ++p)
            if (players[p].entity == ai_entities[i].entity) owner_is_player = 1;
        if (owner_is_player) continue;
        const void *pointer = 0;
        if (api->component(self, ai_entities[i].entity, targets.hash, &pointer) != EnshwickStatus_OK || !pointer) continue;
        ++ready; uint32_t any = 0, primary = 0;
        for (unsigned index = 0; index < 16; ++index) {
            const uint8_t *p = (const uint8_t *)pointer + targets.offset + index * target_entity.size;
            uint32_t entity = (uint32_t)little(p + target_entity.offset, 4);
            uint32_t root = (uint32_t)little(p + target_root.offset, 4);
            for (uint32_t player = 0; player < count; ++player)
                if (players[player].slot < PLAYERS && players[player].entity &&
                    (entity == players[player].entity || root == players[player].entity)) {
                    any |= 1u << players[player].slot;
                    if (index == 0) primary |= 1u << players[player].slot;
                }
        }
        if (!any) continue;
        ++matched; AiCache *c = 0;
        for (unsigned j = 0; j < AI_TRACKED; ++j)
            if (ai_cache[j].entity == ai_entities[i].entity) { c = &ai_cache[j]; break; }
        if (!c) for (unsigned j = 0; j < AI_TRACKED; ++j)
            if (!ai_cache[j].entity) { c = &ai_cache[j]; break; }
        if (!c) { ++overflow; continue; }
        if (heartbeat || c->entity != ai_entities[i].entity || c->any != any || c->primary != primary)
            target_line(t, ai_entities[i].entity, any, primary);
        *c = (AiCache){ai_entities[i].entity, any, primary, 1};
    }
    for (unsigned j = 0; j < AI_TRACKED; ++j)
        if (ai_cache[j].entity && !ai_cache[j].seen) {
            // Disappearance can mean death, despawn, missing component or query
            // truncation; it is not an assertion that combat has ended.
            Line l = {{0}, 0}; prefix(&l, t); text(&l, " ai_candidate="); number(&l, ai_cache[j].entity);
            text(&l, " target_observation=lost"); emit(&l); ai_cache[j].entity = 0;
        }
    if (heartbeat || total != ai_previous_total || overflow) {
        Line l = {{0}, 0}; prefix(&l, t); text(&l, " ai_enumerated="); number(&l, total);
        text(&l, " selected_ready="); number(&l, ready); text(&l, " matched="); number(&l, matched);
        text(&l, " query_truncated="); number(&l, total > AI_LIMIT);
        text(&l, " cache_overflow="); number(&l, overflow); emit(&l);
    }
    ai_previous_total = total;
}

typedef struct { const char *name; uint32_t width; uint64_t offset; int valid; } EventField;
typedef struct {
    const char *name; uint32_t hash, size; int enabled;
    EventField fields[16];
} EventSchema;
#define EF(name, width) {name, width, 0, 0}
static EventSchema events[] = {
    {"keen::ecs::HitEvent", 0, 0, 0, {
        EF("rootSourceId",4), EF("targetId",4), EF("rootTargetId",4), EF("sourceId",4),
        EF("weaponPideId",4), EF("flags",4), EF("healthChange",4), EF("staminaChange",4),
        EF("damageTypes",2), EF("targetFaction",1), EF("weaponCategory",4), EF("attackCategory",1),
        EF("hasMiningVolume",1), EF("miningRemovedVolume",4), EF("timeStamp",8)}},
    {"keen::ecs::HandleEquipmentSlotChangedEvent", 0, 0, 0, {
        EF("playerEntityId",4), EF("equipmentSlot",1), EF("timeStamp",8)}},
    {"keen::ecs::ResourceChangeEvent", 0, 0, 0, {
        EF("targetEntityId",4), EF("staminaChange",8), EF("healthChange",8), EF("timeStamp",8)}},
    {"keen::ecs::EntityDiedEvent", 0, 0, 0, {EF("deadEntityId",4), EF("timeStamp",8)}},
};
#define EVENT_COUNT (sizeof(events) / sizeof(events[0]))
static EnshwickEventTypeInfo event_types[EVENT_TYPES_LIMIT];
static int equal_text(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static const char *event_field_owner(const EventSchema *e, const EventField *f) {
    if (equal_text(f->name, "timeStamp")) return "keen::ecs::GameEvent";
    if (e == &events[0] && (equal_text(f->name, "rootSourceId") ||
        equal_text(f->name, "targetId") || equal_text(f->name, "flags") ||
        equal_text(f->name, "hasMiningVolume") || equal_text(f->name, "miningRemovedVolume")))
        return "keen::ecs::BaseHitEvent";
    return e->name;
}
static int same_name(EnshwickStr actual, const char *wanted) {
    EnshwickStr full = str(wanted);
    const char *short_name = wanted;
    for (const char *p = wanted; *p; ++p) if (*p == ':') short_name = p + 1;
    EnshwickStr short_str = str(short_name);
    EnshwickStr no_keen = str(wanted + 6); // Also accept ecs::HitEvent.
    EnshwickStr expect = actual.len == full.len ? full :
                         actual.len == no_keen.len ? no_keen : short_str;
    if (actual.len != expect.len) return 0;
    for (size_t i = 0; i < actual.len; ++i) if (actual.ptr[i] != expect.ptr[i]) return 0;
    return 1;
}
static uint32_t event_entity(const EventSchema *schema, const EnshwickGameEventData *event, unsigned index) {
    const EventField *f = &schema->fields[index];
    if (!f->valid || !range_ok(f->offset, 4, event->size)) return 0;
    return (uint32_t)little(event->data + f->offset, 4);
}
static int is_player(uint32_t entity) {
    if (!entity) return 0;
    if (api->player) {
        EnshwickPlayerInfo p = {.header = HEADER(EnshwickPlayerInfo)};
        if (api->player(self, entity, &p) == EnshwickStatus_OK) return 1;
    }
    for (unsigned i = 0; i < PLAYERS; ++i) if (cache[i].seen && cache[i].entity == entity) return 1;
    return 0;
}
static void on_game_event(const EnshwickGameEventData *event, void *user) {
    EventSchema *schema = (EventSchema *)user;
    if (!event || !event->data || event->header.size < sizeof(*event) ||
        event->header.abi_major != ENSHWICK_ABI_MAJOR || event->type_hash != schema->hash) return;
    if (event->size != schema->size) {
        if (event_lines++ < EVENT_LIMIT) diagnostic_status("event_size_mismatch", EnshwickStatus_BAD_ARGS);
        else ++event_dropped;
        return;
    }
    int relevant = is_player(event_entity(schema, event, 0));
    if (schema == &events[0]) relevant |= is_player(event_entity(schema, event, 1)) |
                                       is_player(event_entity(schema, event, 2)) |
                                       is_player(event_entity(schema, event, 3));
    if (!relevant && schema != &events[3]) return;
    if (event_lines >= EVENT_LIMIT) { ++event_dropped; return; }
    ++event_lines;
    Line l = {{0}, 0}; text(&l, "event game="); number(&l, game);
    text(&l, " after_step="); number(&l, last_step);
    text(&l, " type="); text(&l, schema->name);
    for (unsigned i = 0; i < 16 && schema->fields[i].name; ++i) {
        EventField *f = &schema->fields[i];
        text(&l, " "); text(&l, f->name); text(&l, "=");
        if (f->valid && range_ok(f->offset, f->width, event->size))
            bytes(&l, event->data + f->offset, f->width);
        else text(&l, "unavailable");
    }
    if (schema == &events[0]) {
        const void *pointer = 0;
        uint32_t target = event_entity(schema, event, 2);
        if (!target) target = event_entity(schema, event, 1);
        EnshwickStatus s = faction.available && target ? api->component(self, target, faction.hash, &pointer) : EnshwickStatus_MISSING;
        text(&l, " target_faction_component_status="); number(&l, s);
        if (s == EnshwickStatus_OK && pointer) { text(&l, " faction_raw="); bytes(&l, (const uint8_t *)pointer + faction.offset, 1); }
    }
    emit(&l);
}
static void event_window(void) {
    if (event_dropped) { Line l = {{0},0}; text(&l,"event_rate_limit dropped="); number(&l,event_dropped); emit(&l); }
    event_lines = event_dropped = 0;
}
static void reset_diagnostics(void) {
    last_step = 0; event_lines = event_dropped = 0;
    for (unsigned p = 0; p < PLAYERS; ++p) stamina_cache[p].known = 0;
    for (unsigned i = 0; i < AI_TRACKED; ++i) ai_cache[i].entity = 0;
    ai_previous_total = UINT32_MAX;
}
static void init_diagnostics(void) {
    reset_diagnostics(); init_stamina();
    faction = resolve("keen::ecs::Faction", "faction", 1, 1);
    targets = resolve("keen::ecs::SelectedTargets", "targets", 1, 1);
    target_entity = resolve("keen::ecs::TargetInfo", "targetEntityId", 4, 0);
    target_root = resolve("keen::ecs::TargetInfo", "targetRootEntityId", 4, 0);
    ai_available = api->entities && targets.available && target_entity.available && target_root.available &&
        target_entity.size == target_root.size && target_entity.size > 0 &&
        range_ok(targets.offset, (uint64_t)target_entity.size * 16, targets.size);
    enemy_hash = 0;
    if (api->type_hash(str("keen::ecs::Enemy01BehaviorState"), &enemy_hash) != EnshwickStatus_OK) enemy_hash = 0;
    diagnostic_status("ai_schema", ai_available ? EnshwickStatus_OK : EnshwickStatus_MISSING);
    if (!api->event_types || !api->game_event) {
        diagnostic_status("event_api", EnshwickStatus_MISSING); return;
    }
    uint32_t count = 0;
    for (unsigned i = 0; i < EVENT_TYPES_LIMIT; ++i) event_types[i].header = (EnshwickHeader)HEADER(EnshwickEventTypeInfo);
    EnshwickStatus s = api->event_types(self, event_types, EVENT_TYPES_LIMIT, &count);
    diagnostic_status("event_catalog", s);
    if (s != EnshwickStatus_OK) return;
    if (count > EVENT_TYPES_LIMIT) { diagnostic_status("event_catalog_truncated", EnshwickStatus_BAD_ARGS); count = EVENT_TYPES_LIMIT; }
    for (unsigned i = 0; i < EVENT_COUNT; ++i) {
        EventSchema *e = &events[i]; e->enabled = 0;
        for (uint32_t j = 0; j < count; ++j) if (same_name(event_types[j].name, e->name)) {
            e->hash = event_types[j].hash; e->size = event_types[j].size; e->enabled = e->hash != 0; break;
        }
        if (!e->enabled) { diagnostic_status(e->name, EnshwickStatus_MISSING); continue; }
        for (unsigned j = 0; j < 16 && e->fields[j].name; ++j) {
            EventField *f = &e->fields[j];
            // Reflection lists inherited fields on their declaring base. As in
            // Enshwick EventType::inherited_field, offsets are from the same
            // prefix; validate both declaring and complete event boundaries.
            Field resolved = resolve(event_field_owner(e, f), f->name, f->width, 0);
            f->offset = resolved.offset;
            f->valid = resolved.available && resolved.size <= e->size &&
                       range_ok(resolved.offset, f->width, e->size);
        }
        s = api->game_event(self, e->hash, on_game_event, e);
        diagnostic_status(e->name, s); e->enabled = s == EnshwickStatus_OK;
    }
}
