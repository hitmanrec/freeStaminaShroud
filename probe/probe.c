// Enshwick sprint preview: guarded code patch, read-only component diagnostics.
#include "enshwick.h"

#define HEADER(T) {sizeof(T), ENSHWICK_ABI_MAJOR, ENSHWICK_ABI_MINOR}
#define PLAYERS 16
#define FIELD_COUNT 15
#define PERIOD_NS UINT64_C(250000000)

static const EnshwickManifest manifest = {
    .header = HEADER(EnshwickManifest),
    .id = ENSHWICK_STR_INIT("hitmanrec.stamina-probe"),
    .version = ENSHWICK_STR_INIT("0.4.0"),
    .side = EnshwickSide_SERVER,
    .name = ENSHWICK_STR_INIT("Free Stamina Shroud - Sprint Preview"),
    .description = ENSHWICK_STR_INIT("Experimental free sprint outside observed combat, with diagnostics."),
    .license = ENSHWICK_STR_INIT("MIT"),
};

typedef struct {
    const char *type, *field;
    uint32_t width, hash, size;
    uint64_t offset;
    int available;
} Field;

// Expected field widths come from the researched reflection headers, NOT
// hard-coded memory offsets. Values are logged as bytes, without interpreting
// time units, float values, or claiming that a stance means active combat.
static Field fields[FIELD_COUNT] = {
    {"keen::ecs::CombatStance", "combatStanceDuration", 8, 0, 0, 0, 0},
    {"keen::ecs::CombatStance", "combatStanceEndTime", 8, 0, 0, 0, 0},
    {"keen::ecs::ClientLocomotion", "inCombatStance", 1, 0, 0, 0, 0},
    {"keen::ecs::ControlCombatTarget", "targetId", 4, 0, 0, 0, 0},
    {"keen::ecs::StaminaDepletion", "accumulatedValue", 4, 0, 0, 0, 0},
    {"keen::ecs::NetworkStamina", "stamina", 2, 0, 0, 0, 0},
    {"keen::ecs::Actor", "currentState", 8, 0, 0, 0, 0},
    {"keen::ecs::ActorInput", "sequenceTriggerId", 4, 0, 0, 0, 0},
    {"keen::ecs::ActorInput", "locomotionSpeed", 1, 0, 0, 0, 0},
    {"keen::ecs::ClientLocomotion", "currentGait", 1, 0, 0, 0, 0},
    {"keen::ecs::DynamicLocomotion", "state", 1, 0, 0, 0, 0},
    {"keen::ecs::DynamicLocomotion", "jumpCount", 1, 0, 0, 0, 0},
    {"keen::ecs::DynamicLocomotion", "lastJumpTime", 8, 0, 0, 0, 0},
    {"keen::ecs::EquipmentAnimationSets", "primaryAnimationSet", 1, 0, 0, 0, 0},
    {"keen::ecs::EquipmentAnimationSets", "secondaryAnimationSet", 1, 0, 0, 0, 0},
};
typedef struct {
    EnshwickEntity entity;
    uint32_t seen, known[FIELD_COUNT], status[FIELD_COUNT];
    uint8_t bytes[FIELD_COUNT][8];
} PlayerCache;
static PlayerCache cache[PLAYERS];
static const EnshwickHostApi *api;
static EnshwickModHandle self;
static uint64_t game, samples;
static uint32_t previous_status = UINT32_MAX, previous_count = UINT32_MAX;

typedef struct { char text[768]; size_t used; } Line;
static EnshwickStr str(const char *s) {
    EnshwickStr result = {(const uint8_t *)s, 0};
    while (s[result.len]) ++result.len;
    return result;
}
static void text(Line *l, const char *s) {
    while (*s && l->used < sizeof(l->text)) l->text[l->used++] = *s++;
}
static void number(Line *l, uint64_t n) {
    char digits[20]; unsigned count = 0;
    do { digits[count++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (count && l->used < sizeof(l->text)) l->text[l->used++] = digits[--count];
}
static void bytes(Line *l, const uint8_t *p, uint32_t n) {
    static const char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < n && l->used + 2 <= sizeof(l->text); ++i) {
        l->text[l->used++] = hex[p[i] >> 4];
        l->text[l->used++] = hex[p[i] & 15];
    }
}
static void emit(Line *l) {
    EnshwickStr message = {(const uint8_t *)l->text, l->used};
    api->log(self, EnshwickLevel_INFO, message);
}
static void clear_cache(void) {
    for (unsigned p = 0; p < PLAYERS; ++p) {
        cache[p].entity = 0; cache[p].seen = 0;
        for (unsigned f = 0; f < FIELD_COUNT; ++f) cache[p].known[f] = 0;
    }
}
static void prefix(Line *l, const EnshwickTickData *tick) {
    text(l, "sample game="); number(l, tick->game);
    text(l, " step="); number(l, tick->step);
}

#include "diagnostics.h"
#include "cost_trace.h"
#include "sprint.h"

static void tick(EnshwickEvent event, const void *data, void *user) {
    (void)user;
    if (event != EnshwickEvent_TICKED || !data) return;
    const EnshwickTickData *t = (const EnshwickTickData *)data;
    if (t->header.size < sizeof(*t) || t->header.abi_major != ENSHWICK_ABI_MAJOR) return;
    if (game != t->game) {
        clear_cache(); game = t->game; samples = 0;
        reset_diagnostics();
        reset_cost_trace();
        reset_sprint_players();
        previous_status = previous_count = UINT32_MAX;
    }
    const int heartbeat = (++samples % 20 == 1);
    event_window();
    EnshwickPlayerInfo players[PLAYERS];
    for (unsigned p = 0; p < PLAYERS; ++p) {
        players[p].header = (EnshwickHeader)HEADER(EnshwickPlayerInfo);
        cache[p].seen = 0;
    }
    uint32_t count = 0;
    EnshwickStatus status = api->players(self, players, PLAYERS, &count);
    if (heartbeat || status != previous_status || count != previous_count) {
        Line l = {{0}, 0}; prefix(&l, t); text(&l, " players_status="); number(&l, status);
        text(&l, " players="); number(&l, count);
        if (count > PLAYERS) text(&l, " truncated=true");
        emit(&l); previous_status = status; previous_count = count;
    }
    if (status != EnshwickStatus_OK) {
        for (unsigned i=0;i<PLAYERS;++i) atomic_store(&sprint_players[i].allowed,0);
        clear_cache(); return;
    }
    if (count > PLAYERS) count = PLAYERS;
    drain_cost_trace(t, players, count);
    for (uint32_t p = 0; p < count; ++p) {
        const EnshwickPlayerInfo *player = &players[p];
        // Slots are defined by the loader as 0..15; reject invalid data.
        if (player->slot >= PLAYERS || !player->entity) continue;
        PlayerCache *c = &cache[player->slot]; c->seen = 1;
        if (c->entity != player->entity) {
            c->entity = player->entity;
            for (unsigned f = 0; f < FIELD_COUNT; ++f) c->known[f] = 0;
        }
        sample_stamina(t, player->slot, player->entity, heartbeat);
        for (unsigned f = 0; f < FIELD_COUNT; ++f) {
            Field *field = &fields[f];
            if (!field->available) continue;
            const void *component = 0;
            status = api->component(self, player->entity, field->hash, &component);
            if (status == EnshwickStatus_OK && !component) status = EnshwickStatus_MISSING;
            uint8_t value[8];
            int changed = !c->known[f] || c->status[f] != status;
            if (status == EnshwickStatus_OK) {
                const uint8_t *source = (const uint8_t *)component + field->offset;
                for (unsigned b = 0; b < field->width; ++b) {
                    value[b] = source[b];
                    if (!c->known[f] || value[b] != c->bytes[f][b]) changed = 1;
                }
            }
            if (heartbeat || changed) {
                Line l = {{0}, 0}; prefix(&l, t);
                text(&l, " slot="); number(&l, player->slot);
                text(&l, " entity="); number(&l, player->entity);
                text(&l, " "); text(&l, field->type); text(&l, "."); text(&l, field->field);
                text(&l, " status="); number(&l, status);
                if (status == EnshwickStatus_OK) {
                    text(&l, " raw_le="); bytes(&l, value, field->width);
                }
                emit(&l);
            }
            c->known[f] = 1; c->status[f] = status;
            if (status == EnshwickStatus_OK)
                for (unsigned b = 0; b < field->width; ++b) c->bytes[f][b] = value[b];
        }
    }
    sample_targets(t, players, count, heartbeat);
    publish_sprint(t,players,count,heartbeat);
    for (unsigned p = 0; p < PLAYERS; ++p)
        if (!cache[p].seen) {
            cache[p].entity = 0;
            stamina_cache[p].known = 0;
            for (unsigned f = 0; f < FIELD_COUNT; ++f) cache[p].known[f] = 0;
        }
}

uint32_t enshwick_mod_abi(void) { return ENSHWICK_ABI_MAJOR; }
const EnshwickManifest *enshwick_mod_query(void) { return &manifest; }
EnshwickStatus enshwick_mod_init(const EnshwickHostApi *host, EnshwickModHandle me) {
    if (!host || host->header.abi_major != ENSHWICK_ABI_MAJOR ||
        host->header.abi_minor < ENSHWICK_ABI_MINOR ||
        host->header.size < sizeof(EnshwickHostApi) || !host->log ||
        !host->build || !host->type_hash || !host->type_size ||
        !host->field_offset || !host->players || !host->component ||
        !host->subscribe_tick) return EnshwickStatus_REFUSED;
    api = host; self = me; game = samples = 0; clear_cache();
    EnshwickBuildInfo build = {.header = HEADER(EnshwickBuildInfo)};
    api->build(&build);
    Line l = {{0}, 0};
    text(&l, "probe=0.4.0 read_only=false sprint_preview=true loader=");
    for (size_t i = 0; i < host->host_version.len && l.used < sizeof(l.text); ++i)
        l.text[l.used++] = (char)host->host_version.ptr[i];
    text(&l, " binary_side="); number(&l, build.side);
    text(&l, " build_table="); number(&l, build.table);
    text(&l, " fingerprint_guid_le="); bytes(&l, build.fingerprint.guid, 16);
    text(&l, " fingerprint_age="); number(&l, build.fingerprint.age); emit(&l);
    for (unsigned f = 0; f < FIELD_COUNT; ++f) {
        Field *field = &fields[f]; field->available = 0;
        EnshwickStatus s = api->type_hash(str(field->type), &field->hash);
        if (s == EnshwickStatus_OK) s = api->type_size(str(field->type), &field->size);
        if (s == EnshwickStatus_OK)
            s = api->field_offset(str(field->type), str(field->field), &field->offset);
        if (s == EnshwickStatus_MISSING && equal_text(field->type, "keen::ecs::Actor") &&
            equal_text(field->field, "currentState")) {
            // Actor inherits currentState from BaseActor; query Actor storage.
            s = api->field_offset(str("keen::ecs::BaseActor"), str(field->field), &field->offset);
        }
        if (s == EnshwickStatus_OK &&
            (field->offset > field->size || field->width > field->size - field->offset))
            s = EnshwickStatus_BAD_ARGS;
        field->available = s == EnshwickStatus_OK;
        Line line = {{0}, 0}; text(&line, "resolve "); text(&line, field->type);
        text(&line, "."); text(&line, field->field); text(&line, " status="); number(&line, s);
        if (field->available) {
            text(&line, " offset="); number(&line, field->offset);
            text(&line, " width="); number(&line, field->width);
            text(&line, " type_size="); number(&line, field->size);
        }
        emit(&line);
    }
    init_diagnostics();
    init_cost_trace(&build);
    log_cost_systems(&build);
    init_sprint(&build);
    EnshwickTickHandle handle = 0;
    return api->subscribe_tick(self, PERIOD_NS, tick, 0, &handle);
}
void enshwick_mod_ready(const EnshwickHostApi *host, EnshwickModHandle me) {
    (void)host; (void)me;
}
