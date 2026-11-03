#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../probe/probe.c"

static EnshwickHostApi fake;
static EnshwickEventFn callback;
static unsigned calls, reads, checks;
static uint32_t player_count = 2;
static EnshwickStatus player_status = EnshwickStatus_OK;
static uint8_t components[PLAYERS][16];
static char logs[131072];
static size_t log_size;
static int bad_offset;
static EnshwickStatus subscription_status = EnshwickStatus_OK;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void log_line(EnshwickModHandle me, EnshwickLevel level, EnshwickStr s) {
    (void)me; (void)level;
    CHECK(log_size + s.len + 2 < sizeof(logs));
    memcpy(logs + log_size, s.ptr, s.len); log_size += s.len;
    logs[log_size++] = '\n'; logs[log_size] = 0; ++calls;
}
static void build(EnshwickBuildInfo *out) {
    out->side = EnshwickSide_CLIENT; out->table = EnshwickBuildTable_EXACT;
    out->fingerprint.age = 520;
}
static EnshwickStatus hash(EnshwickStr name, uint32_t *out) {
    (void)name; *out = 42; return EnshwickStatus_OK;
}
static EnshwickStatus size_of_type(EnshwickStr name, uint32_t *out) {
    (void)name; *out = 16; return EnshwickStatus_OK;
}
static EnshwickStatus offset(EnshwickStr type, EnshwickStr field, uint64_t *out) {
    (void)type;
    if (field.len == strlen("inCombatStance") &&
        memcmp(field.ptr, "inCombatStance", field.len) == 0) return EnshwickStatus_MISSING;
    *out = bad_offset ? UINT64_MAX : 0; return EnshwickStatus_OK;
}
static EnshwickStatus players(EnshwickModHandle me, EnshwickPlayerInfo *out,
                              uint32_t capacity, uint32_t *count) {
    (void)me; *count = player_count;
    if (player_status != EnshwickStatus_OK) return player_status;
    for (uint32_t p = 0; p < player_count && p < capacity; ++p) {
        CHECK(out[p].header.size == sizeof(*out));
        out[p].entity = p + 1; out[p].slot = p;
    }
    return EnshwickStatus_OK;
}
static EnshwickStatus component(EnshwickModHandle me, EnshwickEntity entity,
                                uint32_t type, const void **out) {
    (void)me; (void)type; ++reads;
    CHECK(entity > 0 && entity <= PLAYERS);
    *out = components[entity - 1]; return EnshwickStatus_OK;
}
static EnshwickStatus subscribe(EnshwickModHandle me, uint64_t period,
                                EnshwickEventFn fn, void *user, EnshwickTickHandle *out) {
    (void)me; (void)user; CHECK(period == PERIOD_NS);
    callback = fn; *out = 1; return subscription_status;
}
static void sample(uint64_t world, uint64_t step) {
    EnshwickTickData t = {.header = HEADER(EnshwickTickData), .game = world, .step = step};
    callback(EnshwickEvent_TICKED, &t, 0);
}
int main(void) {
    fake.header = (EnshwickHeader)HEADER(EnshwickHostApi);
    fake.host_version = (EnshwickStr)ENSHWICK_STR_INIT("fake-test-host");
    fake.log = log_line; fake.build = build; fake.type_hash = hash;
    fake.type_size = size_of_type; fake.field_offset = offset;
    fake.players = players; fake.component = component; fake.subscribe_tick = subscribe;
    CHECK(enshwick_mod_abi() == ENSHWICK_ABI_MAJOR);
    CHECK(enshwick_mod_query()->side == EnshwickSide_SERVER);
    CHECK(enshwick_mod_init(0, 1) == EnshwickStatus_REFUSED);
    fake.header.abi_major++;
    CHECK(enshwick_mod_init(&fake, 1) == EnshwickStatus_REFUSED);
    fake.header.abi_major--; fake.header.size--;
    CHECK(enshwick_mod_init(&fake, 1) == EnshwickStatus_REFUSED);
    fake.header.size++; fake.header.abi_minor = 0;
    CHECK(enshwick_mod_init(&fake, 1) == EnshwickStatus_REFUSED);
    fake.header.abi_minor = ENSHWICK_ABI_MINOR;
    CHECK(enshwick_mod_init(&fake, 1) == EnshwickStatus_OK);
    CHECK(strstr(logs, "inCombatStance status=1") != 0);
    sample(1, 1);
    CHECK(strstr(logs, "slot=0 entity=1") != 0);
    CHECK(strstr(logs, "slot=1 entity=2") != 0);
    unsigned before = calls;
    sample(1, 2); CHECK(calls == before);
    components[1][0] = 7;
    sample(1, 3); CHECK(calls > before);
    CHECK(strstr(logs, "raw_le=0700000000000000") != 0);
    CHECK(components[0][0] == 0 && components[1][0] == 7);
    // Dropped players and new games must not retain old cache state.
    player_count = 0; sample(1, 4); CHECK(cache[0].entity == 0);
    player_count = 2; sample(1, 5); CHECK(cache[1].entity == 2);
    before = calls; sample(2, 1); CHECK(calls > before);
    player_status = EnshwickStatus_OUTSIDE_WORLD;
    before = reads; sample(2, 2); CHECK(reads == before);
    player_status = EnshwickStatus_OK; player_count = 20;
    sample(2, 3); CHECK(strstr(logs, "truncated=true") != 0);
    // Bad reflected offsets are rejected before any component pointer read.
    bad_offset = 1;
    CHECK(enshwick_mod_init(&fake, 1) == EnshwickStatus_OK);
    before = reads; sample(3, 1); CHECK(reads == before);
    CHECK(strstr(logs, "combatStanceEndTime status=4") != 0);
    subscription_status = EnshwickStatus_MISSING;
    CHECK(enshwick_mod_init(&fake, 1) == EnshwickStatus_MISSING);
    printf("Probe fake-host tests: %u checks passed\n", checks);
    return 0;
}
