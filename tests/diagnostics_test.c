#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../probe/probe.c"

static unsigned checks, calls;
static char logs[262144];
static size_t used;
static EnshwickHostApi fake;
static EnshwickEventFn callback;
static EnshwickGameEventFn event_callbacks[EVENT_COUNT];
static void *event_users[EVENT_COUNT];
static uint8_t stamina[40], selected[192], actor[80], faction_value = 6;
static uint8_t guest_stamina[40], guest_depletion[4];
static uint32_t ai_total = 1;
static EnshwickStatus ai_status = EnshwickStatus_OK;
static int invalid_constant;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static int eq(EnshwickStr a, const char *b) { return a.len == strlen(b) && memcmp(a.ptr,b,a.len)==0; }
static void put(uint8_t *p, uint64_t n, unsigned width) { for(unsigned i=0;i<width;++i)p[i]=(uint8_t)(n>>(8*i)); }
static void log_line(EnshwickModHandle me, EnshwickLevel level, EnshwickStr message) {
    (void)me;(void)level; CHECK(used + message.len + 2 < sizeof(logs));
    memcpy(logs+used,message.ptr,message.len);used+=message.len;logs[used++]='\n';logs[used]=0;++calls;
}
static void build(EnshwickBuildInfo *out) { out->side=EnshwickSide_CLIENT; }
static EnshwickStatus hash(EnshwickStr name,uint32_t *out) {
    if(eq(name,"keen::ecs::Stamina"))*out=10;
    else if(eq(name,"keen::ecs::Faction"))*out=11;
    else if(eq(name,"keen::ecs::SelectedTargets"))*out=12;
    else if(eq(name,"keen::ecs::Enemy01BehaviorState"))*out=13;
    else if(eq(name,"keen::ecs::Actor"))*out=14;
    else return EnshwickStatus_MISSING;
    return EnshwickStatus_OK;
}
static EnshwickStatus type_size(EnshwickStr name,uint32_t *out) {
    if(eq(name,"keen::ecs::Stamina"))*out=40;
    else if(eq(name,"keen::ecs::Faction"))*out=1;
    else if(eq(name,"keen::ecs::SelectedTargets"))*out=192;
    else if(eq(name,"keen::ecs::TargetInfo"))*out=12;
    else if(eq(name,"keen::ecs::BaseHitEvent"))*out=128;
    else if(eq(name,"keen::ecs::GameEvent"))*out=8;
    else if(eq(name,"keen::ecs::Actor"))*out=80;
    else {
        for(unsigned i=0;i<EVENT_COUNT;++i)if(eq(name,events[i].name)){*out=256;return EnshwickStatus_OK;}
        return EnshwickStatus_MISSING;
    }
    return EnshwickStatus_OK;
}
static EnshwickStatus offset(EnshwickStr type,EnshwickStr field,uint64_t *out) {
    if(eq(type,"keen::ecs::GameEvent") && eq(field,"timeStamp"))*out=0;
    else if(eq(type,"keen::ecs::BaseActor") && eq(field,"currentState"))*out=32;
    else if(eq(type,"keen::ecs::BaseHitEvent")) {
        uint64_t position=0;
        for(unsigned j=0;j<16 && events[0].fields[j].name;++j) {
            if(eq(field,events[0].fields[j].name)){*out=position;return EnshwickStatus_OK;}
            position+=events[0].fields[j].width;
        }
        return EnshwickStatus_MISSING;
    }
    else if(eq(type,"keen::ecs::Faction") && eq(field,"faction"))*out=0;
    else if(eq(type,"keen::ecs::SelectedTargets") && eq(field,"targets"))*out=0;
    else if(eq(type,"keen::ecs::TargetInfo") && eq(field,"targetEntityId"))*out=0;
    else if(eq(type,"keen::ecs::TargetInfo") && eq(field,"targetRootEntityId"))*out=4;
    else if(eq(type,"keen::ecs::TargetInfo") && eq(field,"flags"))*out=8;
    else if(eq(type,"keen::ecs::TargetInfo") && eq(field,"targetImportance"))*out=9;
    else {
        for(unsigned i=0;i<EVENT_COUNT;++i)if(eq(type,events[i].name)) {
            uint64_t position=0;
            for(unsigned j=0;j<16 && events[i].fields[j].name;++j) {
                if(eq(field,events[i].fields[j].name)) {
                    if(!equal_text(event_field_owner(&events[i],&events[i].fields[j]),events[i].name))
                        return EnshwickStatus_MISSING;
                    *out=position;return EnshwickStatus_OK;
                }
                position+=events[i].fields[j].width;
            }
        }
        return EnshwickStatus_MISSING;
    }
    return EnshwickStatus_OK;
}
static EnshwickStatus constant(EnshwickModHandle me,EnshwickStr name,uint64_t *out) {
    (void)me;
    if(eq(name,"ecs.level_data_offset"))*out=invalid_constant?UINT64_MAX:4;
    else if(eq(name,"ecs.level_flags"))*out=6;
    else if(eq(name,"ecs.attribute_count"))*out=7;
    else if(eq(name,"attribute.stamina"))*out=0x04b6aa8b;
    else if(eq(name,"attribute.stamina_max_slot"))*out=2;
    else return EnshwickStatus_MISSING;
    return EnshwickStatus_OK;
}
static EnshwickStatus component(EnshwickModHandle me,EnshwickEntity entity,uint32_t type,const void **out) {
    (void)me;
    if(type==10 && entity==1)*out=stamina;
    else if(type==10 && entity==2)*out=guest_stamina;
    else if(type==14 && entity==1)*out=actor;
    else if(type==12 && entity==100)*out=selected;
    else if(type==11 && entity==100)*out=&faction_value;
    else if(type==15 && entity==1)*out=stamina;
    else if(type==15 && entity==2)*out=guest_depletion;
    else return EnshwickStatus_MISSING;
    return EnshwickStatus_OK;
}
static EnshwickStatus player(EnshwickModHandle me,EnshwickEntity entity,EnshwickPlayerInfo *out) {
    (void)me;if(entity!=1 && entity!=2)return EnshwickStatus_MISSING;
    out->entity=entity;out->slot=entity==1?0:7;return EnshwickStatus_OK;
}
static EnshwickStatus players(EnshwickModHandle me,EnshwickPlayerInfo *out,uint32_t capacity,uint32_t *count) {
    (void)me;CHECK(capacity>0);*count=1;out[0].entity=1;out[0].slot=0;return EnshwickStatus_OK;
}
static EnshwickStatus entities(EnshwickModHandle me,uint32_t type,EnshwickEntityPlace *out,uint32_t capacity,uint32_t *count) {
    (void)me;CHECK(type==12);CHECK(capacity==AI_LIMIT);*count=ai_total;
    for(unsigned i=0;i<ai_total && i<capacity;++i)out[i].entity=i+100;
    return ai_status;
}
static EnshwickStatus subscribe(EnshwickModHandle me,uint64_t period,EnshwickEventFn fn,void *user,EnshwickTickHandle *out) {
    (void)me;(void)user;CHECK(period==PERIOD_NS);callback=fn;*out=1;return EnshwickStatus_OK;
}
static EnshwickStatus event_catalog(EnshwickModHandle me,EnshwickEventTypeInfo *out,uint32_t capacity,uint32_t *count) {
    (void)me;CHECK(capacity>=EVENT_COUNT);*count=EVENT_COUNT;
    for(unsigned i=0;i<EVENT_COUNT;++i){out[i].name=str(events[i].name);out[i].hash=1000+i;out[i].size=256;}
    return EnshwickStatus_OK;
}
static EnshwickStatus subscribe_event(EnshwickModHandle me,uint32_t hash_value,EnshwickGameEventFn fn,void *user) {
    (void)me;CHECK(hash_value>=1000 && hash_value<1000+EVENT_COUNT);
    event_callbacks[hash_value-1000]=fn;event_users[hash_value-1000]=user;return EnshwickStatus_OK;
}
static void sample(uint64_t world,uint64_t step) {
    EnshwickTickData t={.header=HEADER(EnshwickTickData),.game=world,.step=step};callback(EnshwickEvent_TICKED,&t,0);
}
int main(void) {
    CHECK(same_name(str("HitEvent"), "keen::ecs::HitEvent"));
    CHECK(same_name(str("ecs::HitEvent"), "keen::ecs::HitEvent"));
    CHECK(!same_name(str("OtherEvent"), "keen::ecs::HitEvent"));
    fake.header=(EnshwickHeader)HEADER(EnshwickHostApi);
    fake.log=log_line;fake.build=build;fake.type_hash=hash;fake.type_size=type_size;
    fake.field_offset=offset;fake.constant=constant;fake.component=component;
    fake.players=players;fake.player=player;fake.entities=entities;fake.subscribe_tick=subscribe;
    fake.event_types=event_catalog;fake.game_event=subscribe_event;
    put(stamina,0x04b6aa8b,4);put(stamina+4,8,2);stamina[6]=1;stamina[7]=8;
    put(stamina+8,100,4);put(stamina+16,120,4);put(selected,1,4);
    CHECK(enshwick_mod_init(&fake,1)==EnshwickStatus_OK);
    CHECK(stamina_layout.available && ai_available);
    CHECK(fields[6].available && fields[6].hash==14 && fields[6].offset==32);
    uint64_t ignored=0;
    CHECK(offset(str(events[0].name),str("rootSourceId"),&ignored)==EnshwickStatus_MISSING);
    CHECK(events[0].fields[0].valid && events[0].fields[1].valid && events[0].fields[5].valid);
    for(unsigned i=0;i<EVENT_COUNT;++i)CHECK(event_callbacks[i]!=0);
    sample(1,1);
    CHECK(strstr(logs,"stamina_status=0 current=100 maximum=120")!=0);
    CHECK(strstr(logs,"ai_candidate=100 player_slot_mask=1 primary_slot_mask=1")!=0);
    put(stamina+8,90,4);sample(1,2);
    CHECK(strstr(logs,"stamina_status=0 current=90 maximum=120")!=0);
    uint32_t current=0,maximum=0;
    put(stamina+4,39,2);CHECK(read_stamina(1,&current,&maximum)==EnshwickStatus_BAD_ARGS);
    put(stamina+4,8,2);stamina[6]=0;CHECK(read_stamina(1,&current,&maximum)==EnshwickStatus_NOT_LOADED);stamina[6]=1;
    // A failed enumeration must not claim a previously observed target was lost.
    ai_status=EnshwickStatus_MISSING;unsigned before=calls;sample(1,3);CHECK(!observed_ai_complete);
    ai_status=EnshwickStatus_OK;put(selected,0,4);sample(1,4);
    CHECK(strstr(logs,"ai_candidate=100 target_observation=lost")!=0);
    ai_total=AI_LIMIT+1;sample(1,5);CHECK(strstr(logs,"query_truncated=1")!=0);ai_total=1;
    uint8_t hit[256]={0};
    put(hit+events[0].fields[0].offset,1,4);
    put(hit+events[0].fields[1].offset,100,4);put(hit+events[0].fields[2].offset,100,4);
    EnshwickGameEventData event={.header=HEADER(EnshwickGameEventData),.type_hash=1000,.size=256,.data=hit};
    event_callbacks[0](&event,event_users[0]);
    CHECK(strstr(logs,"type=keen::ecs::HitEvent rootSourceId=01000000")!=0);
    CHECK(strstr(logs,"target_faction_component_status=0 faction_raw=06")!=0);
    // If a build lacks rootSourceId, sourceId still retains outgoing hits.
    int valid=events[0].fields[0].valid;events[0].fields[0].valid=0;
    put(hit+events[0].fields[3].offset,1,4);before=calls;
    event_callbacks[0](&event,event_users[0]);CHECK(calls==before+1);
    events[0].fields[0].valid=valid;put(hit+events[0].fields[3].offset,0,4);
    // Ignore unrelated hits. A wrong-sized event never reaches field reads.
    put(hit+events[0].fields[0].offset,200,4);before=calls;
    event_callbacks[0](&event,event_users[0]);CHECK(calls==before);
    put(hit+events[0].fields[0].offset,1,4);event.size=1;
    event_callbacks[0](&event,event_users[0]);CHECK(strstr(logs,"event_size_mismatch status=4")!=0);event.size=256;
    // Optional fields outside the event buffer are not dereferenced.
    uint64_t saved=events[0].fields[4].offset;events[0].fields[4].offset=UINT64_MAX;
    event_callbacks[0](&event,event_users[0]);CHECK(strstr(logs,"weaponPideId=unavailable")!=0);events[0].fields[4].offset=saved;
    event_window();
    for(unsigned i=0;i<EVENT_LIMIT+3;++i)event_callbacks[0](&event,event_users[0]);
    CHECK(event_dropped==3);event_window();CHECK(strstr(logs,"event_rate_limit dropped=3")!=0);
    sample(2,1);CHECK(game==2 && samples==1);
    // Preview eligibility is per entity, short lived, and fails closed on
    // incomplete AI observations. Exercise the same publication used in game.
    reset_sprint_players();sprint_installed=1;sprint_test_time=100;
    fields[4].available=1;fields[4].hash=15;fields[4].offset=0;
    EnshwickPlayerInfo one={.slot=0,.entity=1};
    EnshwickTickData tick_data={.game=2,.step=10};
    observed_ai_complete=1;observed_ai_mask=0;
    publish_sprint(&tick_data,&one,1,0);
    CHECK(sprint_gate((uintptr_t)stamina,4));
    // Live 0.4.1 session: pointer matched, but ground sprint selector was 0.
    CHECK(sprint_gate((uintptr_t)stamina,0));
    CHECK(!sprint_gate((uintptr_t)stamina,9));
    CHECK(!sprint_gate((uintptr_t)stamina+4,4));
    sprint_test_time=451;CHECK(!sprint_gate((uintptr_t)stamina,4));
    publish_sprint(&tick_data,&one,1,0);CHECK(sprint_gate((uintptr_t)stamina,4));
    observed_ai_complete=0;publish_sprint(&tick_data,&one,1,0);
    CHECK(!sprint_gate((uintptr_t)stamina,4));
    observed_ai_complete=1;observed_ai_mask=1;publish_sprint(&tick_data,&one,1,0);
    CHECK(!sprint_gate((uintptr_t)stamina,4));
    CHECK(!sprint_gate((uintptr_t)stamina,0));
    observed_ai_mask=0;sprint_test_time=10450;publish_sprint(&tick_data,&one,1,0);
    CHECK(!sprint_gate((uintptr_t)stamina,4));
    sprint_test_time=10451;publish_sprint(&tick_data,&one,1,0);
    CHECK(sprint_gate((uintptr_t)stamina,4));
    put(hit+events[0].fields[5].offset,0x4000,4);
    note_sprint_event(&events[0],&event);CHECK(sprint_gate((uintptr_t)stamina,4));
    put(hit+events[0].fields[5].offset,0,4);
    note_sprint_event(&events[0],&event);CHECK(!sprint_gate((uintptr_t)stamina,4));
    sprint_test_time+=10000;publish_sprint(&tick_data,&one,1,0);
    CHECK(sprint_gate((uintptr_t)stamina,4));
    // Incoming hits also block. Malformed events cannot grant free sprint.
    put(hit+events[0].fields[0].offset,100,4);
    put(hit+events[0].fields[1].offset,1,4);put(hit+events[0].fields[2].offset,1,4);
    note_sprint_event(&events[0],&event);CHECK(!sprint_gate((uintptr_t)stamina,4));
    sprint_test_time+=10000;publish_sprint(&tick_data,&one,1,0);
    CHECK(sprint_gate((uintptr_t)stamina,4));
    event.size=1;note_sprint_event(&events[0],&event);event.size=256;
    CHECK(!sprint_gate((uintptr_t)stamina,4));
    CHECK(atomic_load(&sprint_counters[SPRINT_CALLS])>0);
    CHECK(atomic_load(&sprint_counters[SPRINT_SELECTOR_OK])>0);
    CHECK(atomic_load(&sprint_counters[SPRINT_POINTER_OK])>0);
    CHECK(atomic_load(&sprint_counters[SPRINT_OTHER_SELECTOR])==1);
    CHECK(atomic_load(&sprint_counters[SPRINT_UNKNOWN_POINTER])==1);
    CHECK(atomic_load(&sprint_counters[SPRINT_NOT_ALLOWED])>0);
    CHECK(atomic_load(&sprint_counters[SPRINT_EXPIRED])==1);
    CHECK(atomic_load(&sprint_counters[SPRINT_BLOCKED])>0);
    CHECK(atomic_load(&sprint_selector_mask)==(1u|(1u<<4)|(1u<<9)));
    CHECK(atomic_load(&sprint_matched_selector_mask)==(1u|(1u<<4)|(1u<<9)));
    publish_sprint(&tick_data,&one,1,1);
    CHECK(strstr(logs," sprint_gate calls=")!=0);
    CHECK(atomic_load(&sprint_counters[SPRINT_CALLS])==0);
    CHECK(atomic_load(&sprint_selector_mask)==0);
    reset_sprint_players();CHECK(!sprint_gate((uintptr_t)stamina,4));
    observed_ai_complete=0;sample_targets(&tick_data,&one,1,0);CHECK(observed_ai_complete);
    ai_status=EnshwickStatus_MISSING;sample_targets(&tick_data,&one,1,0);CHECK(!observed_ai_complete);
    ai_status=EnshwickStatus_OK;
    // Regression: a noncombat target category must not block free sprint.
    reset_sprint_players();sprint_test_time=60000;
    memset(selected,0,sizeof(selected));put(selected+12,1,4);
    selected[20]=3;selected[21]=2;
    sample_targets(&tick_data,&one,1,0);
    CHECK(observed_ai_candidate_mask==1 && observed_ai_mask==0 && observed_ai_complete);
    publish_sprint(&tick_data,&one,1,0);CHECK(sprint_gate((uintptr_t)stamina,0));
    CHECK(strstr(logs,"index_mask=2 combat_selected=0")!=0);
    CHECK(strstr(logs,"index=1 player_mask=1 flags=3 importance=2")!=0);
    // Only host is selected in combat. Guest is in a nonsequential slot,
    // listed first: permission/counters must follow slot and entity identity.
    EnshwickPlayerInfo pair[2]={{.slot=7,.entity=2},{.slot=0,.entity=1}};
    memset(selected,0,sizeof(selected));put(selected,1,4);
    sample_targets(&tick_data,pair,2,0);CHECK(observed_ai_mask==1);
    publish_sprint(&tick_data,pair,2,0);
    CHECK(!sprint_gate((uintptr_t)stamina,0));
    CHECK(sprint_gate((uintptr_t)guest_depletion,0));
    CHECK(atomic_load(&sprint_players[0].blocked)>0);
    CHECK(atomic_load(&sprint_players[7].skipped)==1);
    CHECK(atomic_load(&sprint_players[0].skipped)==0); // Drained on state change.
    // Incoming guest hit blocks the guest immediately, independent of AI.
    put(hit+events[0].fields[0].offset,100,4);
    put(hit+events[0].fields[1].offset,2,4);put(hit+events[0].fields[2].offset,2,4);
    note_sprint_event(&events[0],&event);
    CHECK(!sprint_gate((uintptr_t)guest_depletion,0));
    CHECK(atomic_load(&sprint_players[7].combat_hits)==1);
    CHECK(atomic_load(&sprint_players[0].combat_hits)==0);
    // Both regain eligibility after targets disappear and cooldown elapses.
    memset(selected,0,sizeof(selected));sample_targets(&tick_data,pair,2,0);
    sprint_test_time+=10000;publish_sprint(&tick_data,pair,2,1);
    CHECK(sprint_gate((uintptr_t)stamina,0));CHECK(sprint_gate((uintptr_t)guest_depletion,0));
    CHECK(strstr(logs,"sprint_player slot=7 entity=2 allowed=1")!=0);
    // Outgoing guest hit must not block the idle host.
    put(hit+events[0].fields[0].offset,2,4);
    put(hit+events[0].fields[1].offset,100,4);put(hit+events[0].fields[2].offset,100,4);
    note_sprint_event(&events[0],&event);
    CHECK(!sprint_gate((uintptr_t)guest_depletion,0));CHECK(sprint_gate((uintptr_t)stamina,0));
    sprint_test_time+=10000;publish_sprint(&tick_data,pair,2,1);
    CHECK(sprint_gate((uintptr_t)guest_depletion,0));
    // Stamina sampling is per character and preserves distinct values.
    memcpy(guest_stamina,stamina,sizeof(stamina));put(guest_stamina+8,77,4);
    sample_stamina(&tick_data,7,2,1);
    CHECK(strstr(logs,"entity=2 stamina_status=0 current=77 maximum=120")!=0);
    CHECK(atomic_load(&sprint_players[7].skipped)==1);
    publish_sprint(&tick_data,&one,1,0);
    CHECK(!sprint_gate((uintptr_t)guest_depletion,0));
    CHECK(atomic_load(&sprint_players[7].pointer)==0);
    CHECK(strstr(logs,"sprint_player_left slot=7 entity=2")!=0);
    publish_sprint(&tick_data,pair,2,0);
    CHECK(sprint_gate((uintptr_t)guest_depletion,0));
    CHECK(atomic_load(&sprint_players[7].skipped)==1); // No previous occupant's count.
    publish_sprint(&tick_data,pair,0,0);
    CHECK(!sprint_gate((uintptr_t)stamina,0));CHECK(!sprint_gate((uintptr_t)guest_depletion,0));
    invalid_constant=1;init_stamina();CHECK(!stamina_layout.available);
    CHECK(strstr(logs,"stamina_schema status=4")!=0);
    CHECK(little(stamina+8,4)==90); // Diagnostics did not alter the meter.
    printf("Extended diagnostics tests: %u checks passed\n",checks);
    return 0;
}
