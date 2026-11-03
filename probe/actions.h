// Direct costs: only two reviewed call sites, live classification on the
// sampled server thread, and exact identity of a connected player's Stamina.
#define TOOL_RETURN UINT64_C(0x223b0f)
#define JUMP_RETURN UINT64_C(0x2c97f9)
enum { ACTION_UNKNOWN, ACTION_FREE_TOOL, ACTION_FREE_JUMP, ACTION_THREAD,
       ACTION_PLAYER, ACTION_STALE, ACTION_ITEM, ACTION_COMBAT, ACTION_JUMP_TYPE,
       ACTION_ITEM_LOOKUP, ACTION_ITEM_CATEGORY, ACTION_ITEM_ANIMATION };
// Stable item resource id, NOT the session weapon PIDE. Identified by repeated
// mining HitEvents and same-actor cost records in the user's /205 sessions.
#define REVIEWED_PICKAXE UINT32_C(2671192505)
typedef struct { _Atomic(uintptr_t) attribute; _Atomic(uint32_t) entity; } ActionPlayer;
static ActionPlayer action_players[PLAYERS];
static _Atomic(uint32_t) action_thread;
static _Atomic(uint64_t) action_lease;
static int actions_ready;
static Field action_slot, action_item_id, action_jump;
#ifdef _WIN32
__declspec(dllimport) uint32_t __stdcall GetCurrentThreadId(void);
static uint32_t action_current_thread(void){return GetCurrentThreadId();}
#else
static uint32_t action_test_thread=1;
static uint32_t action_current_thread(void){return action_test_thread;}
#endif
static void reset_actions(void) {
    atomic_store(&action_thread,0);atomic_store(&action_lease,0);
    for(unsigned i=0;i<PLAYERS;++i){atomic_store(&action_players[i].attribute,0);atomic_store(&action_players[i].entity,0);}
}
static void publish_actions(const EnshwickPlayerInfo *players,uint32_t count) {
    atomic_store(&action_thread,0);
    for(unsigned i=0;i<PLAYERS;++i){atomic_store(&action_players[i].attribute,0);atomic_store(&action_players[i].entity,0);}
    for(uint32_t i=0;i<count;++i)if(players[i].slot<PLAYERS && players[i].entity && stamina_layout.available) {
        const void *p=0;
        if(api->component(self,players[i].entity,stamina_layout.hash,&p)!=EnshwickStatus_OK || !p)continue;
        ActionPlayer *entry=&action_players[players[i].slot];
        atomic_store(&entry->entity,players[i].entity);atomic_store(&entry->attribute,(uintptr_t)p);
    }
    atomic_store(&action_lease,sprint_clock()+SPRINT_LEASE_MS);
    atomic_store_explicit(&action_thread,action_current_thread(),memory_order_release);
}
static int32_t action_delta(void *attribute,int32_t delta,uint64_t caller,uint32_t *reason,uint32_t *detail) {
    *reason=ACTION_UNKNOWN;*detail=0;
    if(!actions_ready || delta>=0 || (caller!=TOOL_RETURN && caller!=JUMP_RETURN))return delta;
    // No ECS calls on workers, client prediction, or before a server tick.
    uint32_t thread=atomic_load_explicit(&action_thread,memory_order_acquire);
    if(!thread || thread!=action_current_thread()){*reason=ACTION_THREAD;return delta;}
    uint64_t now=sprint_clock();
    if(now>atomic_load(&action_lease)){*reason=ACTION_STALE;return delta;}
    unsigned slot=PLAYERS;uint32_t entity=0;
    for(unsigned i=0;i<PLAYERS;++i)if(atomic_load(&action_players[i].attribute)==(uintptr_t)attribute){
        slot=i;entity=atomic_load(&action_players[i].entity);break;}
    if(slot==PLAYERS || !entity){*reason=ACTION_PLAYER;return delta;}
    // Revalidate through the server-thread API before dereferencing any
    // component: cached addresses are identities, never stale read pointers.
    const void *live=0;
    if(api->component(self,entity,stamina_layout.hash,&live)!=EnshwickStatus_OK || live!=attribute){
        *reason=ACTION_PLAYER;return delta;}
    if(caller==JUMP_RETURN) {
        SprintPlayer *p=&sprint_players[slot];
        if(atomic_load(&p->entity)!=entity || !atomic_load(&p->allowed) ||
           now>atomic_load(&p->lease) || now<atomic_load(&p->blocked_until)){
            *reason=ACTION_COMBAT;return delta;}
        *reason=ACTION_JUMP_TYPE;
        if(!action_jump.available || api->component(self,entity,action_jump.hash,&live)!=EnshwickStatus_OK || !live)return delta;
        uint32_t type=((const uint8_t *)live)[action_jump.offset];*detail=type;
        // Default, double default/sprint/mountain, sprint, mountain and slide.
        // Exclude attack, submerged, glider and grappling jump types.
        if(type!=1 && type!=2 && type!=3 && type!=4 && type!=5 && type!=14 && type!=15)return delta;
        *reason=ACTION_FREE_JUMP;return 0;
    }
    *reason=ACTION_ITEM;
    if(!action_slot.available || !action_item_id.available)return delta;
    if(api->component(self,entity,action_slot.hash,&live)!=EnshwickStatus_OK || !live)return delta;
    uint32_t item=(uint32_t)little((const uint8_t *)live+action_slot.offset+action_item_id.offset,4);
    *detail=item;
    if(item==REVIEWED_PICKAXE){*reason=ACTION_FREE_TOOL;return 0;}
    if(!api->item_info || !fields[13].available)return delta;
    EnshwickItemData info={.header=HEADER(EnshwickItemData)};
    if(!item || api->item_info(self,item,&info)!=EnshwickStatus_OK){*reason=ACTION_ITEM_LOOKUP;return delta;}
    if(info.category!=3){*reason=ACTION_ITEM_CATEGORY;return delta;}
    if(api->component(self,entity,fields[13].hash,&live)!=EnshwickStatus_OK || !live ||
       ((const uint8_t *)live)[fields[13].offset]!=4){*reason=ACTION_ITEM_ANIMATION;return delta;}
    // Tools + TwoHanded excludes swords/axes classed as combat weapons (2).
    // No target/material/combat distinction: terrain, creatures and air count.
    *reason=ACTION_FREE_TOOL;return 0;
}
static void init_actions(const EnshwickBuildInfo *build) {
    actions_ready=0;reset_actions();
    action_slot=resolve("keen::ecs::SlotSelection","actionItem",20,1);
    action_item_id=resolve("keen::ItemInstanceInfo","itemId",4,0);
    // The request is written before the debit; DynamicLocomotion.jumpType is
    // applied later and can still describe the preceding jump (or be Invalid).
    action_jump=resolve("keen::ecs::ActorInput","externalTriggerJumpType",1,1);
    if(action_item_id.size!=20)action_item_id.available=0;
    if(api->inventory_open)diagnostic_status("action_inventory",api->inventory_open(self));
    uint64_t begin=0,length=0;
    if(!cost_installed || !reviewed_client_build(build) || !api->code_range ||
       build->base>UINT64_MAX-JUMP_RETURN || api->code_range(&begin,&length)!=EnshwickStatus_OK ||
       build->base+UINT64_C(0x223af7)<begin || build->base+UINT64_C(0x2c9684)<begin ||
       !range_ok(build->base+UINT64_C(0x223af7)-begin,24,length) ||
       !range_ok(build->base+UINT64_C(0x2c9684)-begin,23,length) ||
       !range_ok(build->base+UINT64_C(0x2c97d0)-begin,41,length)){
        diagnostic_status("action_cost_guard",EnshwickStatus_REFUSED);return;}
    if(sprint_code_digest((const uint8_t *)(uintptr_t)(build->base+UINT64_C(0x223af7)),24)!=UINT32_C(0xdec87622) ||
       sprint_code_digest((const uint8_t *)(uintptr_t)(build->base+UINT64_C(0x2c9684)),23)!=UINT32_C(0x75f3353c) ||
       sprint_code_digest((const uint8_t *)(uintptr_t)(build->base+UINT64_C(0x2c97d0)),41)!=UINT32_C(0xec1d643e)){
        diagnostic_status("action_cost_sites",EnshwickStatus_REFUSED);return;}
    actions_ready=1;diagnostic_status("action_cost_guard",EnshwickStatus_OK);
    Line l={{0},0};text(&l,"action_rules tools_schema=");
    number(&l,action_slot.available && action_item_id.available && fields[13].available && api->item_info);
    text(&l," jump_schema=");number(&l,action_jump.available);
    text(&l," thread_guard=true tool_category=3 tool_animation=4");emit(&l);
}
