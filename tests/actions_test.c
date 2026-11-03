#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../probe/probe.c"
static unsigned checks,component_reads,info_reads;
static int32_t meters[2]={100,100},last_delta;
static uint8_t slots[2][216],animations[2],jumps[2][456];
static uint64_t return_address;
static int fail_component,fail_info;
static EnshwickHostApi fake;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));}
static EnshwickStatus component(EnshwickModHandle me,EnshwickEntity entity,uint32_t hash,const void **out){
    (void)me;++component_reads;if(fail_component || entity<1 || entity>2)return EnshwickStatus_MISSING;
    unsigned i=entity-1;
    if(hash==10)*out=&meters[i];else if(hash==11)*out=slots[i];else if(hash==12)*out=&animations[i];
    else if(hash==13)*out=jumps[i];else return EnshwickStatus_MISSING;
    return EnshwickStatus_OK;
}
static EnshwickStatus item_info(EnshwickModHandle me,EnshwickItemId item,EnshwickItemData *out){
    (void)me;++info_reads;if(fail_info)return EnshwickStatus_MISSING;
    out->category=item==101?3:item==102?2:0;return EnshwickStatus_OK;
}
static void frame(EnshwickCallerFrame *out){out->rsp=(uintptr_t)&return_address;}
static void original(void *attribute,const void *registry,int32_t delta,uint32_t root){
    (void)registry;(void)root;last_delta=delta;*(int32_t *)attribute+=delta;
}
static void invoke(void *attribute,int32_t delta,uint32_t root,uint64_t caller,int32_t expected,unsigned reason){
    return_address=cost_base+caller;
    unsigned before=cost_count;cost_observe(attribute,0,delta,root);CHECK(last_delta==expected);
    if(delta<0 && root==COST_ROOT){CHECK(cost_count==before+1);CHECK(cost_queue[before].reason==reason);CHECK(cost_queue[before].applied==expected);}
}
int main(void){
    fake.component=component;fake.item_info=item_info;fake.caller_frame=frame;api=&fake;self=1;
    cost_base=0x10000000;cost_code_begin=cost_base;cost_code_size=0x400000;
    atomic_store(&cost_next,(uintptr_t)original);
    stamina_layout.available=1;stamina_layout.hash=10;
    action_slot=(Field){.available=1,.hash=11,.offset=168,.size=216};
    action_item_id=(Field){.available=1,.offset=8,.size=20};
    action_jump=(Field){.available=1,.hash=13,.offset=340,.size=456};
    fields[13]=(Field){.available=1,.hash=12,.offset=0,.size=1};
    EnshwickPlayerInfo players[2]={{.slot=0,.entity=1},{.slot=7,.entity=2}};
    animations[0]=animations[1]=4;put32(slots[0]+176,101);put32(slots[1]+176,101);
    sprint_test_time=100;action_test_thread=1;actions_ready=1;
    reset_actions();reset_sprint_players();publish_actions(players,2);
    for(unsigned i=0;i<2;++i){unsigned slot=i?7:0;atomic_store(&sprint_players[slot].entity,i+1);
        atomic_store(&sprint_players[slot].allowed,1);atomic_store(&sprint_players[slot].lease,450);}
    // Tools are free even while their player is blocked for combat.
    atomic_store(&sprint_players[0].allowed,0);atomic_store(&sprint_players[0].blocked_until,10000);
    invoke(&meters[0],-3,COST_ROOT,TOOL_RETURN,0,ACTION_FREE_TOOL);
    CHECK(meters[0]==100);
    invoke(&meters[1],-4,COST_ROOT,TOOL_RETURN,0,ACTION_FREE_TOOL);CHECK(meters[1]==100);
    // Switch to a two-handed combat weapon between ticks: classify live item,
    // not old animation or old equipment snapshot.
    put32(slots[0]+176,102);invoke(&meters[0],-3,COST_ROOT,TOOL_RETURN,-3,ACTION_ITEM);
    put32(slots[0]+176,101);animations[0]=7;invoke(&meters[0],-3,COST_ROOT,TOOL_RETURN,-3,ACTION_ITEM);
    animations[0]=4;fail_info=1;invoke(&meters[0],-3,COST_ROOT,TOOL_RETURN,-3,ACTION_ITEM);fail_info=0;
    // Neither delta size nor an unknown caller/root grants free stamina.
    invoke(&meters[0],-3,COST_ROOT,0x1234,-3,ACTION_UNKNOWN);
    invoke(&meters[0],-3,123,TOOL_RETURN,-3,ACTION_UNKNOWN);
    invoke(&meters[0],2,COST_ROOT,TOOL_RETURN,2,ACTION_UNKNOWN);
    int32_t npc=100;invoke(&npc,-3,COST_ROOT,TOOL_RETURN,-3,ACTION_PLAYER);CHECK(npc==97);
    // Workers do not enter ECS or item APIs.
    unsigned reads=component_reads,infos=info_reads;action_test_thread=2;
    invoke(&meters[0],-3,COST_ROOT,TOOL_RETURN,-3,ACTION_THREAD);
    CHECK(component_reads==reads && info_reads==infos);action_test_thread=1;
    sprint_test_time=451;invoke(&meters[0],-3,COST_ROOT,TOOL_RETURN,-3,ACTION_STALE);
    sprint_test_time=100;
    // Jump costs share combat eligibility, but only reviewed ground jump types.
    jumps[0][340]=2;invoke(&meters[0],-10,COST_ROOT,JUMP_RETURN,-10,ACTION_COMBAT);
    atomic_store(&sprint_players[0].allowed,1);atomic_store(&sprint_players[0].blocked_until,0);
    const unsigned allowed[]={1,2,3,4,5,14,15};
    for(unsigned i=0;i<sizeof(allowed)/sizeof(*allowed);++i){jumps[0][340]=(uint8_t)allowed[i];
        invoke(&meters[0],-15,COST_ROOT,JUMP_RETURN,0,ACTION_FREE_JUMP);}
    const unsigned denied[]={0,6,7,8,9,10,11,12,13,16,255};
    for(unsigned i=0;i<sizeof(denied)/sizeof(*denied);++i){jumps[0][340]=(uint8_t)denied[i];
        invoke(&meters[0],-10,COST_ROOT,JUMP_RETURN,-10,ACTION_JUMP_TYPE);}
    jumps[1][340]=2;invoke(&meters[1],-10,COST_ROOT,JUMP_RETURN,0,ACTION_FREE_JUMP);
    atomic_store(&sprint_players[0].allowed,0);
    invoke(&meters[1],-10,COST_ROOT,JUMP_RETURN,0,ACTION_FREE_JUMP); // Host combat doesn't block guest.
    fail_component=1;invoke(&meters[1],-10,COST_ROOT,JUMP_RETURN,-10,ACTION_PLAYER);fail_component=0;
    actions_ready=0;invoke(&meters[0],-3,COST_ROOT,TOOL_RETURN,-3,ACTION_UNKNOWN);actions_ready=1;
    reset_actions();invoke(&meters[0],-3,COST_ROOT,TOOL_RETURN,-3,ACTION_THREAD);
    printf("Direct action cost tests: %u checks passed\n",checks);return 0;
}
