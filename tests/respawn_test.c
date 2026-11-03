#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../probe/probe.c"
static unsigned checks,writes,subscriptions;
static uint32_t fail_write;
static uint8_t pools[2][3][40],actors[2][3832];
static EnshwickHostApi fake;
static char output[32768];static size_t used;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int eq(EnshwickStr a,const char *s){return a.len==strlen(s) && !memcmp(a.ptr,s,a.len);}
static void log_line(EnshwickModHandle me,EnshwickLevel level,EnshwickStr s){
    (void)me;(void)level;CHECK(used+s.len+2<sizeof(output));memcpy(output+used,s.ptr,s.len);used+=s.len;output[used++]='\n';output[used]=0;
}
static EnshwickStatus hash(EnshwickStr type,uint32_t *out){
    const char *names[]={"keen::ecs::Health","keen::ecs::Stamina","keen::ecs::Mana"};
    for(unsigned k=0;k<3;++k)if(eq(type,names[k])){*out=100+k;return EnshwickStatus_OK;}
    if(eq(type,"keen::ecs::Actor")){*out=20;return EnshwickStatus_OK;}
    return EnshwickStatus_MISSING;
}
static EnshwickStatus size_of(EnshwickStr type,uint32_t *out){
    uint32_t ignored;
    if(eq(type,"keen::ecs::Actor"))*out=3832;
    else if(hash(type,&ignored)==EnshwickStatus_OK)*out=40;
    else return EnshwickStatus_MISSING;
    return EnshwickStatus_OK;
}
static EnshwickStatus offset(EnshwickStr type,EnshwickStr field,uint64_t *out){
    (void)type;
    if(eq(field,"currentColliderMode"))*out=3760;else return EnshwickStatus_MISSING;
    return EnshwickStatus_OK;
}
static EnshwickStatus constant(EnshwickModHandle me,EnshwickStr name,uint64_t *out){
    (void)me;
    const char *roots[]={"attribute.health","attribute.stamina","attribute.mana"};
    const char *maxima[]={"attribute.health_max_slot","attribute.stamina_max_slot","attribute.mana_max_slot"};
    for(unsigned k=0;k<3;++k){if(eq(name,roots[k])){*out=1000+k;return EnshwickStatus_OK;}
        if(eq(name,maxima[k])){*out=1;return EnshwickStatus_OK;}}
    return EnshwickStatus_MISSING;
}
static EnshwickStatus component(EnshwickModHandle me,EnshwickEntity entity,uint32_t type,const void **out){
    (void)me;if(entity<1 || entity>3)return EnshwickStatus_MISSING;
    unsigned i=entity==1?0:1;
    if(type==20)*out=actors[i];else if(type>=100 && type<103)*out=pools[i][type-100];
    else return EnshwickStatus_MISSING;
    return EnshwickStatus_OK;
}
static EnshwickStatus writable(EnshwickModHandle me,EnshwickEntity entity,uint32_t type,void **out){
    ++writes;if(type==fail_write)return EnshwickStatus_REFUSED;
    const void *p=0;EnshwickStatus s=component(me,entity,type,&p);*out=(void *)p;return s;
}
static EnshwickStatus subscribe(EnshwickModHandle me,EnshwickEvent event,EnshwickEventFn fn,void *user){
    (void)me;(void)user;CHECK(fn==respawn_event);
    CHECK(event==EnshwickEvent_PLAYER_LEFT || event==EnshwickEvent_PLAYER_DIED);++subscriptions;return EnshwickStatus_OK;
}
static EnshwickStatus player_info(EnshwickModHandle me,EnshwickEntity entity,EnshwickPlayerInfo *out){
    (void)me;if(entity<1 || entity>3)return EnshwickStatus_MISSING;
    out->entity=entity;out->key=entity==1?11:22;return EnshwickStatus_OK;
}
static void meter(unsigned player,unsigned pool,uint32_t current,uint32_t maximum){
    uint8_t *p=pools[player][pool];memset(p,0,40);respawn_store(p,1000+pool);
    p[4]=8;p[6]=1;p[7]=2;respawn_store(p+8,current);respawn_store(p+12,maximum);
}
static uint32_t value(unsigned player,unsigned pool){return (uint32_t)little(pools[player][pool]+8,4);}
static EnshwickPlayerInfo players[2]={{.slot=0,.entity=1,.key=11},{.slot=7,.entity=2,.key=22}};
static void sample(void){EnshwickTickData tick={.game=1,.step=1};tick_respawn(&tick,players,2,1);sprint_test_time+=250;}
static void died(unsigned i){EnshwickPlayerDeathData d={.header=HEADER(EnshwickPlayerDeathData),.entity=players[i].entity,.key=players[i].key};
    respawn_event(EnshwickEvent_PLAYER_DIED,&d,0);}
int main(void){
    fake.log=log_line;fake.type_hash=hash;fake.type_size=size_of;fake.field_offset=offset;
    fake.constant=constant;fake.component=component;fake.component_write=writable;fake.subscribe=subscribe;api=&fake;self=1;
    fake.player=player_info;
    stamina_layout.available=1;stamina_layout.data=4;stamina_layout.flags=6;stamina_layout.count=7;
    fields[6]=(Field){.available=1,.hash=20,.size=3104};
    EnshwickBuildInfo build={.side=EnshwickSide_CLIENT,.table=EnshwickBuildTable_PATTERNS};
    memcpy(build.fingerprint.guid,client_205_guid,16);build.fingerprint.age=205;
    init_respawn(&build);CHECK(respawn_ready && subscriptions==2);
    for(unsigned i=0;i<2;++i){meter(i,0,40,150);meter(i,1,10,120);meter(i,2,5,80);}
    sample();sample();CHECK(writes==0); // Login/ordinary damage never refills.
    note_respawn_dead_entity(99);sample();CHECK(writes==0); // NPCs don't arm a player's refill.
    meter(0,0,0,150);note_respawn_dead_entity(1);died(0);sample();sample();CHECK(writes==0 && value(0,0)==0);
    sprint_test_time+=700000;sample();CHECK(writes==0); // Long respawn-menu waits stay eligible.
    meter(0,0,30,150);actors[0][3760]=5;sample();CHECK(writes==0);
    actors[0][3760]=9;sample();CHECK(writes==0); // DivingDeath also stays dead.
    actors[0][3760]=0;actors[0][24]=25; // Stale previous action context must not block a live avatar.
    sample();CHECK(writes==0);sample();
    CHECK(writes==3 && value(0,0)==150 && value(0,1)==120 && value(0,2)==80);
    CHECK(value(1,0)==40 && value(1,1)==10 && value(1,2)==5);
    CHECK(little(pools[0][0]+12,4)==150 && pools[0][0][6]==1);
    meter(0,0,50,150);sample();sample();CHECK(writes==3 && value(0,0)==50);
    CHECK(strstr(output,"respawn_refill slot=0 entity=1 health=30->150 stamina=10->120 mana=5->80")!=0);
    // A new character/entity/slot for the same connected guest is restored.
    meter(1,0,0,200);died(1);sample();players[1].entity=3;players[1].slot=9;
    meter(1,0,20,500);meter(1,1,1,250);meter(1,2,0,0);sample();sample();
    CHECK(value(1,0)==500 && value(1,1)==250 && value(1,2)==0 && value(0,0)==50);
    CHECK(strstr(output,"respawn_refill slot=9 entity=3")!=0);
    // Preflight failure cannot partially heal; retry after all views are ready.
    meter(0,0,0,200);died(0);sample();meter(0,0,10,200);meter(0,1,2,140);meter(0,2,3,100);
    sample();fail_write=102;sample();CHECK(value(0,0)==10 && value(0,1)==2 && value(0,2)==3);
    fail_write=0;sample();sample();CHECK(value(0,0)==200 && value(0,1)==140 && value(0,2)==100);
    // Maxima can settle after respawn; use the new values and fill just once.
    meter(0,0,0,200);died(0);sample();meter(0,0,10,200);sample();
    meter(0,0,10,210);sample();CHECK(value(0,0)==10);sample();CHECK(value(0,0)==210);
    // Leaving cancels the pending death even if the account reconnects.
    meter(0,0,0,210);died(0);
    EnshwickPlayerData left={.header=HEADER(EnshwickPlayerData),.key=11,.entity=1};
    respawn_event(EnshwickEvent_PLAYER_LEFT,&left,0);meter(0,0,10,210);
    unsigned before=writes;sample();sample();CHECK(writes==before && value(0,0)==10);
    // Corrupt container boundaries do not yield writes.
    died(0);pools[0][1][4]=39;sample();sample();CHECK(writes==before);
    reset_respawn();pools[0][1][4]=8;
    died(0);sample();sprint_test_time+=10001;sample();CHECK(writes==before);
    build.fingerprint.age=999;init_respawn(&build);CHECK(!respawn_ready);
    died(0);sample();CHECK(writes==before);
    printf("Respawn refill tests: %u checks passed\n",checks);return 0;
}
