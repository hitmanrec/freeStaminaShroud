#include "sprint_stub.h"
#define SPRINT_SITE UINT64_C(0x2f1505)
#define SPRINT_SYSTEM UINT64_C(0x2f1000)
#define SPRINT_LEASE_MS UINT64_C(350)
#define SPRINT_COOLDOWN_MS UINT64_C(10000)
typedef struct {
    _Atomic(uintptr_t) pointer;
    _Atomic(uint64_t) lease, blocked_until;
    _Atomic(uint32_t) entity, allowed;
    _Atomic(uint32_t) matches, skipped, deny_selector, deny_permission, expired, blocked, combat_hits;
    uint32_t logged_entity, logged_allowed, logged_ai, logged_candidates;
    int logged;
} SprintPlayer;
static SprintPlayer sprint_players[PLAYERS];
static int sprint_installed;
static _Atomic(uint32_t) sprint_skipped;
enum { SPRINT_CALLS, SPRINT_SELECTOR_OK, SPRINT_POINTER_OK, SPRINT_OTHER_SELECTOR,
       SPRINT_UNKNOWN_POINTER, SPRINT_NOT_ALLOWED, SPRINT_EXPIRED, SPRINT_BLOCKED,
       SPRINT_COUNTERS };
static _Atomic(uint32_t) sprint_counters[SPRINT_COUNTERS];
static _Atomic(uint32_t) sprint_selector_mask, sprint_matched_selector_mask;
static void sprint_count(unsigned index) {
    atomic_fetch_add_explicit(&sprint_counters[index],1,memory_order_relaxed);
}
static int sprint_selector_supported(uint32_t selector) {
    // This predicate is used ONLY at the guarded sprint accumulation site.
    // Hosted-server traces use 0 for ordinary ground sprint; 4 is the
    // reflected Sprint value. Never extend this to a generic stamina hook.
    return selector==0 || selector==4;
}
#ifdef _WIN32
__declspec(dllimport) uint64_t __stdcall GetTickCount64(void);
__declspec(dllimport) void *__stdcall VirtualAlloc(void *,size_t,uint32_t,uint32_t);
__declspec(dllimport) int __stdcall VirtualFree(void *,size_t,uint32_t);
__declspec(dllimport) int __stdcall VirtualProtect(void *,size_t,uint32_t,uint32_t *);
__declspec(dllimport) void *__stdcall GetCurrentProcess(void);
__declspec(dllimport) int __stdcall FlushInstructionCache(void *,const void *,size_t);
static uint64_t sprint_clock(void){return GetTickCount64();}
#else
static uint64_t sprint_test_time;
static uint64_t sprint_clock(void){return sprint_test_time;}
#endif
static int sprint_gate(uintptr_t accumulator,uint32_t actual_selector) {
    // R14B is captured by the original loop. Exclude swimming/diving and
    // unknown states; reaching this site already requires the game's checks.
    sprint_count(SPRINT_CALLS);
    uint32_t selector_bit=actual_selector<31?1u<<actual_selector:UINT32_C(0x80000000);
    atomic_fetch_or_explicit(&sprint_selector_mask,selector_bit,memory_order_relaxed);
    if(sprint_selector_supported(actual_selector))sprint_count(SPRINT_SELECTOR_OK);
    uint64_t now=sprint_clock();
    for(unsigned i=0;i<PLAYERS;++i) {
        SprintPlayer *p=&sprint_players[i];
        if(atomic_load_explicit(&p->pointer,memory_order_acquire)!=accumulator)continue;
        sprint_count(SPRINT_POINTER_OK);
        atomic_fetch_add_explicit(&p->matches,1,memory_order_relaxed);
        atomic_fetch_or_explicit(&sprint_matched_selector_mask,selector_bit,memory_order_relaxed);
        if(!sprint_selector_supported(actual_selector)){sprint_count(SPRINT_OTHER_SELECTOR);atomic_fetch_add(&p->deny_selector,1);return 0;}
        if(now<atomic_load(&p->blocked_until)){sprint_count(SPRINT_BLOCKED);atomic_fetch_add(&p->blocked,1);return 0;}
        if(!atomic_load_explicit(&p->allowed,memory_order_acquire)){
            sprint_count(SPRINT_NOT_ALLOWED);atomic_fetch_add(&p->deny_permission,1);return 0;}
        if(now>atomic_load(&p->lease)){sprint_count(SPRINT_EXPIRED);atomic_fetch_add(&p->expired,1);return 0;}
        atomic_fetch_add_explicit(&p->skipped,1,memory_order_relaxed);
        atomic_fetch_add_explicit(&sprint_skipped,1,memory_order_relaxed);return 1;
    }
    sprint_count(SPRINT_UNKNOWN_POINTER);return 0;
}
static void reset_sprint_players(void) {
    for(unsigned i=0;i<SPRINT_COUNTERS;++i)atomic_store(&sprint_counters[i],0);
    atomic_store(&sprint_selector_mask,0);atomic_store(&sprint_matched_selector_mask,0);
    for(unsigned i=0;i<PLAYERS;++i){atomic_store(&sprint_players[i].allowed,0);
        atomic_store(&sprint_players[i].pointer,0);atomic_store(&sprint_players[i].entity,0);
        atomic_store(&sprint_players[i].blocked_until,0);
        atomic_store(&sprint_players[i].matches,0);atomic_store(&sprint_players[i].skipped,0);
        atomic_store(&sprint_players[i].deny_selector,0);atomic_store(&sprint_players[i].deny_permission,0);
        atomic_store(&sprint_players[i].expired,0);atomic_store(&sprint_players[i].blocked,0);
        atomic_store(&sprint_players[i].combat_hits,0);
        sprint_players[i].logged=0;}
}
static void block_sprint_entity(uint32_t entity) {
    uint64_t until=sprint_clock()+SPRINT_COOLDOWN_MS;
    for(unsigned i=0;i<PLAYERS;++i)if(atomic_load(&sprint_players[i].entity)==entity){
        atomic_store(&sprint_players[i].allowed,0);atomic_store(&sprint_players[i].blocked_until,until);
        atomic_fetch_add_explicit(&sprint_players[i].combat_hits,1,memory_order_relaxed);
    }
}
static void note_sprint_event(const EventSchema *schema,const EnshwickGameEventData *event) {
    if(schema!=&events[0])return;
    const EventField *flags=&schema->fields[5];
    if(event->size!=schema->size || !flags->valid || !range_ok(flags->offset,4,event->size)) {
        for(unsigned i=0;i<PLAYERS;++i){atomic_store(&sprint_players[i].allowed,0);
            atomic_store(&sprint_players[i].blocked_until,sprint_clock()+SPRINT_COOLDOWN_MS);}
        return;
    }
    if(little(event->data+flags->offset,4)&0x4000)return; // Healing, not combat.
    uint32_t source=event_entity(schema,event,0),target=event_entity(schema,event,2);
    if(!source)source=event_entity(schema,event,3);
    if(!target)target=event_entity(schema,event,1);
    if(!source || source==target)return;
    if(is_player(target))block_sprint_entity(target);
    if(is_player(source) && target && faction.available) {
        const void *p=0;
        if(api->component(self,target,faction.hash,&p)==EnshwickStatus_OK && p)
            block_sprint_entity(source);
    }
}
static void publish_sprint(const EnshwickTickData *t,const EnshwickPlayerInfo *players,uint32_t count,int heartbeat) {
    for(unsigned i=0;i<PLAYERS;++i)atomic_store_explicit(&sprint_players[i].allowed,0,memory_order_release);
    int event_ready=events[0].enabled && events[0].fields[0].valid &&
                    events[0].fields[1].valid && events[0].fields[2].valid && events[0].fields[5].valid;
    uint64_t now=sprint_clock();uint32_t allowed_mask=0,present_mask=0;
    for(uint32_t i=0;i<count;++i) {
        uint32_t slot=players[i].slot;if(slot>=PLAYERS)continue;
        if(!players[i].entity)continue;
        present_mask|=1u<<slot;
        SprintPlayer *p=&sprint_players[slot];
        if(atomic_load(&p->entity)!=players[i].entity){
            atomic_store(&p->blocked_until,0);p->logged=0;
            atomic_store(&p->matches,0);atomic_store(&p->skipped,0);atomic_store(&p->deny_selector,0);
            atomic_store(&p->deny_permission,0);atomic_store(&p->expired,0);atomic_store(&p->blocked,0);
            atomic_store(&p->combat_hits,0);
        }
        atomic_store(&p->entity,players[i].entity);
        if(observed_ai_mask&(1u<<slot))atomic_store(&p->blocked_until,now+SPRINT_COOLDOWN_MS);
        const void *component=0;
        int valid=fields[4].available && api->component(self,players[i].entity,fields[4].hash,&component)==EnshwickStatus_OK && component;
        atomic_store(&p->pointer,valid?(uintptr_t)component+fields[4].offset:0);
        atomic_store(&p->lease,now+SPRINT_LEASE_MS);
        if(sprint_installed && valid && event_ready && observed_ai_complete &&
           now>=atomic_load(&p->blocked_until)) {
            atomic_store_explicit(&p->allowed,1,memory_order_release);allowed_mask|=1u<<slot;
        }
        uint32_t allowed=(allowed_mask>>slot)&1, combat_ai=(observed_ai_mask>>slot)&1;
        uint32_t candidates=(observed_ai_candidate_mask>>slot)&1;
        if(heartbeat || !p->logged || p->logged_entity!=players[i].entity ||
           p->logged_allowed!=allowed || p->logged_ai!=combat_ai || p->logged_candidates!=candidates) {
            Line l={{0},0};prefix(&l,t);text(&l," sprint_player slot=");number(&l,slot);
            text(&l," entity=");number(&l,players[i].entity);text(&l," allowed=");number(&l,allowed);
            text(&l," pointer_valid=");number(&l,valid);text(&l," event_ready=");number(&l,event_ready);
            text(&l," ai_complete=");number(&l,observed_ai_complete);
            text(&l," ai_candidate=");number(&l,candidates);text(&l," ai_combat_target=");number(&l,combat_ai);
            uint64_t until=atomic_load(&p->blocked_until);
            text(&l," cooldown_ms=");number(&l,until>now?until-now:0);
            text(&l," matches=");number(&l,atomic_exchange(&p->matches,0));
            text(&l," skipped=");number(&l,atomic_exchange(&p->skipped,0));
            text(&l," deny_selector=");number(&l,atomic_exchange(&p->deny_selector,0));
            text(&l," deny_permission=");number(&l,atomic_exchange(&p->deny_permission,0));
            text(&l," expired=");number(&l,atomic_exchange(&p->expired,0));
            text(&l," combat_blocked=");number(&l,atomic_exchange(&p->blocked,0));
            text(&l," combat_hits=");number(&l,atomic_exchange(&p->combat_hits,0));
            EnshwickStatus net=api->net_ready?api->net_ready(self,players[i].key):EnshwickStatus_MISSING;
            text(&l," loader_channel_status=");number(&l,net);emit(&l);
            p->logged=1;p->logged_entity=players[i].entity;p->logged_allowed=allowed;
            p->logged_ai=combat_ai;p->logged_candidates=candidates;
        }
    }
    for(unsigned slot=0;slot<PLAYERS;++slot)if(!(present_mask&(1u<<slot))) {
        SprintPlayer *p=&sprint_players[slot];
        uint32_t entity=atomic_load(&p->entity);
        if(entity){Line l={{0},0};prefix(&l,t);text(&l," sprint_player_left slot=");number(&l,slot);
            text(&l," entity=");number(&l,entity);emit(&l);}
        atomic_store(&p->pointer,0);atomic_store(&p->entity,0);atomic_store(&p->blocked_until,0);p->logged=0;
    }
    uint32_t skipped=atomic_exchange(&sprint_skipped,0);
    if(heartbeat || skipped){Line l={{0},0};prefix(&l,t);text(&l," sprint_free_mask=");number(&l,allowed_mask);
        text(&l," ai_complete=");number(&l,observed_ai_complete);text(&l," skipped_sprint_adds=");number(&l,skipped);emit(&l);}
    if(heartbeat) {
        static const char *const names[SPRINT_COUNTERS]={" calls="," selector_sprint="," pointer_matches=",
            " deny_selector="," unknown_pointer="," deny_permission="," expired="," combat_blocked="};
        Line l={{0},0};prefix(&l,t);text(&l," sprint_gate");
        for(unsigned i=0;i<SPRINT_COUNTERS;++i){text(&l,names[i]);number(&l,atomic_exchange(&sprint_counters[i],0));}
        text(&l," selector_mask=");number(&l,atomic_exchange(&sprint_selector_mask,0));
        text(&l," matched_selector_mask=");number(&l,atomic_exchange(&sprint_matched_selector_mask,0));emit(&l);
    }
}
#ifdef _WIN32
static void *allocate_sprint_near(uintptr_t site) {
    uintptr_t aligned=site&~(uintptr_t)0xffff;
    for(uintptr_t i=1;i<0x7000;++i) {
        uintptr_t distance=i*0x10000;
        if(aligned>distance){void *p=VirtualAlloc((void *)(aligned-distance),4096,0x3000,0x04);if(p)return p;}
        if(aligned<UINTPTR_MAX-distance){void *p=VirtualAlloc((void *)(aligned+distance),4096,0x3000,0x04);if(p)return p;}
    }
    return 0;
}
#endif
static void init_sprint(const EnshwickBuildInfo *build) {
    sprint_installed=0;reset_sprint_players();
#ifdef _WIN32
    unsigned identity=reviewed_client_build(build);
    uint64_t shift=reviewed_code_shift(identity);
    uint64_t system=0,begin=0,length=0;
    if(!identity || !api->patch_code || !api->system || !api->code_range ||
       build->base>UINT64_MAX-(SPRINT_SITE+shift) ||
       api->system(str("stamina_depletion_sprint"),&system)!=EnshwickStatus_OK ||
       system!=build->base+SPRINT_SYSTEM+shift || api->code_range(&begin,&length)!=EnshwickStatus_OK ||
       system<begin || !range_ok(system-begin,0x5b3,length)) {
        diagnostic_status("sprint_patch_guard",EnshwickStatus_REFUSED);return;
    }
    const uint8_t prologue[]={0x40,0x55,0x41,0x57,0x48,0x8d,0xac,0x24,0xf8,0xfe,0xff,0xff,0x48,0x81,0xec,0x08,0x02,0,0};
    const uint8_t original[]={0xf3,0x0f,0x58,0x30,0xf3,0x0f,0x11,0x30};
    uintptr_t site=(uintptr_t)(build->base+SPRINT_SITE+shift);
    for(unsigned i=0;i<sizeof(prologue);++i)if(((const uint8_t *)(uintptr_t)system)[i]!=prologue[i]){
        diagnostic_status("sprint_patch_prologue",EnshwickStatus_REFUSED);return;}
    for(unsigned i=0;i<sizeof(original);++i)if(((const uint8_t *)site)[i]!=original[i]){
        diagnostic_status("sprint_patch_site",EnshwickStatus_REFUSED);return;}
    if(!reviewed_sprint_code(identity,(const uint8_t *)(uintptr_t)system,0x5b3)){
        diagnostic_status("sprint_patch_body",EnshwickStatus_REFUSED);return;}
    uint8_t *stub=allocate_sprint_near(site);
    if(!stub){diagnostic_status("sprint_patch_allocation",EnshwickStatus_MISSING);return;}
    size_t size=build_sprint_stub(stub,4096,(uintptr_t)stub,(uintptr_t)sprint_gate,site+8);
    uint32_t old=0;
    if(!size || !VirtualProtect(stub,4096,0x20,&old) || !FlushInstructionCache(GetCurrentProcess(),stub,size)){
        VirtualFree(stub,0,0x8000);diagnostic_status("sprint_patch_code",EnshwickStatus_REFUSED);return;}
    uint8_t replacement[8]={0xe9,0,0,0,0,0x90,0x90,0x90};int32_t relative=0;
    if(!stub_rel32(site+5,(uintptr_t)stub,&relative)){VirtualFree(stub,0,0x8000);return;}
    for(unsigned i=0;i<4;++i)replacement[i+1]=(uint8_t)((uint32_t)relative>>(8*i));
    EnshwickStatus status=api->patch_code(self,site,original,replacement,8);
    sprint_installed=status==EnshwickStatus_OK;
    if(!sprint_installed)VirtualFree(stub,0,0x8000);
    diagnostic_status("sprint_patch_registered",status);
#else
    (void)build;(void)sprint_gate;(void)build_sprint_stub;(void)reviewed_sprint_code;
    diagnostic_status("sprint_patch_platform",EnshwickStatus_REFUSED);
#endif
}
