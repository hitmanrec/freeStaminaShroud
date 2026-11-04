// Verified four-argument helper from the user's exact EXE report. This observer
// forwards unknown calls unchanged. Gameplay classification is main-thread only.
#include <stdatomic.h>
#define COST_RVA UINT64_C(0x21fbc0)
#define COST_ROOT UINT32_C(0x04b6aa8b)
#define COST_CAPACITY 256
typedef void (*CostNext)(void *, const void *, int32_t, uint32_t);
typedef struct { uintptr_t attribute; int32_t delta; uint64_t caller_rva; int32_t applied; uint32_t reason,detail; } CostRecord;
static int cost_installed;
static const uint8_t cost_prologue[] = {
    0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,
    0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x60
};
static _Atomic(uintptr_t) cost_next;
static atomic_flag cost_lock = ATOMIC_FLAG_INIT;
static _Atomic(uint32_t) cost_dropped;
static CostRecord cost_queue[COST_CAPACITY], cost_drain[COST_CAPACITY];
static unsigned cost_count;
static uint64_t cost_base, cost_code_begin, cost_code_size;

static void cost_observe(void *attribute, const void *registry, int32_t delta, uint32_t root) {
    uint64_t caller = 0;
    if (root == COST_ROOT && delta < 0) {
        EnshwickCallerFrame frame = {0};
        api->caller_frame(&frame);
        // CALLER_FRAME supplies the real live entry stack, before calling on.
        if (frame.rsp) {
            uint64_t return_address = *(const uint64_t *)(uintptr_t)frame.rsp;
            if (return_address >= cost_code_begin &&
                return_address - cost_code_begin < cost_code_size && return_address >= cost_base)
                caller = return_address - cost_base;
        }
    }
    // Loader contract: never invokes the detour before this slot is armed.
    CostNext next = (CostNext)atomic_load_explicit(&cost_next, memory_order_acquire);
    uint32_t reason=0,detail=0;
    int32_t applied=root==COST_ROOT && delta<0?action_delta(attribute,delta,caller,&reason,&detail):delta;
    next(attribute, registry, applied, root);
    if (root != COST_ROOT || delta >= 0) return;
    // Non-blocking bounded queue: no allocation, ECS calls, I/O or waiting.
    if (atomic_flag_test_and_set_explicit(&cost_lock, memory_order_acquire)) {
        atomic_fetch_add_explicit(&cost_dropped, 1, memory_order_relaxed); return;
    }
    if (cost_count < COST_CAPACITY)
        cost_queue[cost_count++] = (CostRecord){(uintptr_t)attribute, delta, caller,applied,reason,detail};
    else atomic_fetch_add_explicit(&cost_dropped, 1, memory_order_relaxed);
    atomic_flag_clear_explicit(&cost_lock, memory_order_release);
}
static void reset_cost_trace(void) {
    if (!atomic_flag_test_and_set_explicit(&cost_lock, memory_order_acquire)) {
        cost_count = 0;
        atomic_flag_clear_explicit(&cost_lock, memory_order_release);
    }
}
static void drain_cost_trace(const EnshwickTickData *t, const EnshwickPlayerInfo *players, uint32_t count) {
    if (atomic_flag_test_and_set_explicit(&cost_lock, memory_order_acquire)) return;
    unsigned n = cost_count;
    for (unsigned i = 0; i < n; ++i) cost_drain[i] = cost_queue[i];
    cost_count = 0;
    atomic_flag_clear_explicit(&cost_lock, memory_order_release);
    if (!n && !atomic_load_explicit(&cost_dropped,memory_order_relaxed)) return;
    uintptr_t attributes[PLAYERS] = {0};
    for (uint32_t p = 0; p < count && stamina_layout.available; ++p) {
        const void *pointer = 0;
        if (api->component(self, players[p].entity, stamina_layout.hash, &pointer) == EnshwickStatus_OK)
            attributes[p] = (uintptr_t)pointer;
    }
    unsigned unmatched = 0;
    for (unsigned i = 0; i < n; ++i) {
        int found = 0;
        for (uint32_t p = 0; p < count; ++p) if (attributes[p] && attributes[p] == cost_drain[i].attribute) {
            Line l = {{0},0}; prefix(&l,t); text(&l," cost_trace entity="); number(&l,players[p].entity);
            text(&l," delta="); signed32(&l,(uint32_t)cost_drain[i].delta);
            text(&l," caller_return_rva="); number(&l,cost_drain[i].caller_rva);
            text(&l," forwarded_unchanged=");text(&l,cost_drain[i].applied==cost_drain[i].delta?"true":"false");
            text(&l," applied_delta=");signed32(&l,(uint32_t)cost_drain[i].applied);
            text(&l," action_reason=");number(&l,cost_drain[i].reason);
            text(&l," action_detail=");number(&l,cost_drain[i].detail);emit(&l); found=1; break;
        }
        if (!found) ++unmatched;
    }
    uint32_t dropped = atomic_exchange_explicit(&cost_dropped,0,memory_order_relaxed);
    if (unmatched || dropped) {
        Line l={{0},0};prefix(&l,t);text(&l," cost_trace_unmatched=");number(&l,unmatched);
        text(&l," dropped=");number(&l,dropped);emit(&l);
    }
}
static void init_cost_trace(const EnshwickBuildInfo *build) {
    cost_installed=0;
    EnshwickStatus status = EnshwickStatus_REFUSED;
    unsigned identity=reviewed_client_build(build);
    uint64_t helper_rva=COST_RVA+reviewed_code_shift(identity);
    if (!identity || !api->hook || !api->caller_frame || !api->code_range ||
        !build->base || build->base > UINT64_MAX-helper_rva) {
        diagnostic_status("cost_trace_guard",status); return;
    }
    uint64_t target=build->base+helper_rva;
    status=api->code_range(&cost_code_begin,&cost_code_size);
    if(status!=EnshwickStatus_OK || target<cost_code_begin ||
       !range_ok(target-cost_code_begin,sizeof(cost_prologue),cost_code_size)) {
        diagnostic_status("cost_trace_guard",EnshwickStatus_REFUSED);return;
    }
    const uint8_t *code=(const uint8_t *)(uintptr_t)target;
    for(unsigned i=0;i<sizeof(cost_prologue);++i) if(code[i]!=cost_prologue[i]) {
        diagnostic_status("cost_trace_prologue",EnshwickStatus_REFUSED);return;
    }
    cost_base=build->base;
    EnshwickHookSpec spec={target,(size_t)cost_observe,(const uintptr_t *)&cost_next,0,
                           EnshwickHookFlags_CALLER_FRAME};
    status=api->hook(self,&spec);
    cost_installed=status==EnshwickStatus_OK;
    diagnostic_status("cost_trace_hook",status);
}
static void log_cost_systems(const EnshwickBuildInfo *build) {
    if (!api->system) { diagnostic_status("cost_systems",EnshwickStatus_MISSING);return; }
    const char *names[]={"stamina_depletion_sprint","stamina_depletion_glider",
                         "stamina_depletion_climb_wall","stamina_depletion_finalize",
                         "stamina_recharge","player_control_action_sequence"};
    for (unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i) {
        uint64_t address=0;EnshwickStatus s=api->system(str(names[i]),&address);
        Line l={{0},0};text(&l,"cost_system ");text(&l,names[i]);text(&l," status=");number(&l,s);
        if(s==EnshwickStatus_OK && address>=build->base){text(&l," rva=");number(&l,address-build->base);}
        emit(&l);
    }
}
