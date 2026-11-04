// Read-only measurements around climb_wall's accumulation. Never change
// registers, instructions' results, eligibility, or the accumulator itself.
typedef struct {
    _Atomic(uint32_t) owner_thread,before,after,samples,changed,dropped;
} ClimbTrace;
static ClimbTrace climb_trace[PLAYERS];
static int climb_trace_ready;
static void reset_climb_trace(void) {
    for(unsigned i=0;i<PLAYERS;++i){ClimbTrace *c=&climb_trace[i];
        atomic_store(&c->owner_thread,0);atomic_store(&c->before,0);atomic_store(&c->after,0);
        atomic_store(&c->samples,0);atomic_store(&c->changed,0);atomic_store(&c->dropped,0);}
}
static void climb_measure(EnshwickRegisters *registers,void *user) {
    if(!registers || !climb_trace_ready)return;
    uint64_t now=sprint_clock();unsigned slot=PLAYERS;
    for(unsigned i=0;i<PLAYERS;++i)if(registers->rax &&
       atomic_load(&sprint_players[i].pointer)==registers->rax && now<=atomic_load(&sprint_players[i].lease)){
        slot=i;break;}
    if(slot==PLAYERS)return;
    ClimbTrace *c=&climb_trace[slot];uint32_t thread=action_current_thread();
    if(!user){uint32_t expected=0;
        if(!atomic_compare_exchange_strong(&c->owner_thread,&expected,thread)){
            atomic_fetch_add(&c->dropped,1);return;}
        // RAX is the live operand about to be read by the game's ADDSS,
        // not a dereference of an old cached component address.
        atomic_store(&c->before,(uint32_t)little((const uint8_t *)(uintptr_t)registers->rax,4));
    }else if(atomic_load(&c->owner_thread)==thread){
        uint32_t after=(uint32_t)little((const uint8_t *)(uintptr_t)registers->rax,4);
        atomic_store(&c->after,after);atomic_fetch_add(&c->samples,1);
        if(after!=atomic_load(&c->before))atomic_fetch_add(&c->changed,1);
        atomic_store(&c->owner_thread,0);
    }
}
static void drain_climb_trace(const EnshwickTickData *tick,const EnshwickPlayerInfo *players,uint32_t count,int heartbeat) {
    for(uint32_t i=0;i<count;++i){unsigned slot=players[i].slot;if(slot>=PLAYERS)continue;
        ClimbTrace *c=&climb_trace[slot];
        if(!heartbeat && !atomic_load(&c->changed) && !atomic_load(&c->dropped))continue;
        uint32_t samples=atomic_exchange(&c->samples,0),changed=atomic_exchange(&c->changed,0),dropped=atomic_exchange(&c->dropped,0);
        if(!samples && !dropped)continue;
        Line l={{0},0};prefix(&l,tick);text(&l," climb_trace slot=");number(&l,slot);
        text(&l," entity=");number(&l,players[i].entity);text(&l," samples=");number(&l,samples);
        text(&l," changed=");number(&l,changed);text(&l," dropped=");number(&l,dropped);
        text(&l," before_float_bits=");number(&l,atomic_load(&c->before));
        text(&l," after_float_bits=");number(&l,atomic_load(&c->after));emit(&l);
    }
}
static void init_climb_trace(const EnshwickBuildInfo *build) {
    climb_trace_ready=0;reset_climb_trace();uint64_t system=0,begin=0,length=0;
    unsigned identity=reviewed_client_build(build);uint64_t shift=reviewed_code_shift(identity);
    if(!identity || !api->hook_at || !api->system || !api->code_range ||
       build->base>UINT64_MAX-(UINT64_C(0x2f0a04)+shift) ||
       api->system(str("stamina_depletion_climb_wall"),&system)!=EnshwickStatus_OK ||
       system!=build->base+UINT64_C(0x2f0600)+shift || api->code_range(&begin,&length)!=EnshwickStatus_OK ||
       build->base+UINT64_C(0x2f09f2)+shift<begin ||
       !range_ok(build->base+UINT64_C(0x2f09f2)+shift-begin,18,length) ||
       sprint_code_digest((const uint8_t *)(uintptr_t)(build->base+UINT64_C(0x2f09f2)+shift),18)!=UINT32_C(0x8a76d895)){
        diagnostic_status("climb_trace_guard",EnshwickStatus_REFUSED);return;}
    EnshwickMidHookSpec before={.address=build->base+UINT64_C(0x2f09f6)+shift,.handler=climb_measure};
    EnshwickMidHookSpec after={.address=build->base+UINT64_C(0x2f09fe)+shift,.handler=climb_measure,.user=(void *)(uintptr_t)1};
    EnshwickStatus status=api->hook_at(self,&before);
    if(status==EnshwickStatus_OK)status=api->hook_at(self,&after);
    climb_trace_ready=status==EnshwickStatus_OK;diagnostic_status("climb_trace_hooks",status);
}
