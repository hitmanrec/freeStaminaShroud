// One-shot restoration after a confirmed player death. All callbacks and
// writes run on the server game thread; no raw writes through read-only ECS.
typedef struct { uint32_t hash,size,root,maximum; } RespawnPool;
typedef struct {
    EnshwickPlayerKey key;
    uint32_t dead_entity,entity,pending,stable,maxima[3];
    uint64_t expires;
} RespawnPending;
static RespawnPool respawn_pools[3];
static RespawnPending respawns[PLAYERS];
static Field respawn_actor;
static int respawn_ready;
static void reset_respawn(void) {
    for(unsigned i=0;i<PLAYERS;++i)respawns[i]=(RespawnPending){0};
}
static void respawn_event(EnshwickEvent event,const void *data,void *user) {
    (void)user;
    if(!respawn_ready || !data)return;
    if(event==EnshwickEvent_PLAYER_LEFT){
        const EnshwickPlayerData *p=data;
        if(p->header.size<sizeof(*p) || p->header.abi_major!=ENSHWICK_ABI_MAJOR)return;
        for(unsigned i=0;i<PLAYERS;++i)if(respawns[i].key==p->key){
            if(respawns[i].pending)diagnostic_status("respawn_cancel_left",EnshwickStatus_OK);
            respawns[i]=(RespawnPending){0};}
    }else if(event==EnshwickEvent_PLAYER_DIED){
        const EnshwickPlayerDeathData *p=data;
        if(p->header.size<sizeof(*p) || p->header.abi_major!=ENSHWICK_ABI_MAJOR || !p->key || !p->entity)return;
        RespawnPending *entry=0;
        for(unsigned i=0;i<PLAYERS;++i)if(respawns[i].key==p->key){entry=&respawns[i];break;}
        if(!entry)for(unsigned i=0;i<PLAYERS;++i)if(!respawns[i].pending){entry=&respawns[i];break;}
        if(!entry){diagnostic_status("respawn_queue",EnshwickStatus_REFUSED);return;}
        if(entry->pending && entry->dead_entity==p->entity && !entry->expires)return;
        *entry=(RespawnPending){.key=p->key,.dead_entity=p->entity,.pending=1};
        Line l={{0},0};text(&l,"respawn_death entity=");number(&l,p->entity);
        text(&l," cause=");number(&l,p->cause);emit(&l);
    }
}
static void note_respawn_dead_entity(uint32_t entity) {
    if(!respawn_ready || !entity || !api->player)return;
    EnshwickPlayerInfo player={.header=HEADER(EnshwickPlayerInfo)};
    if(api->player(self,entity,&player)!=EnshwickStatus_OK || player.entity!=entity || !player.key)return;
    // EntityDiedEvent can cover deaths absent from the loader's cause log.
    // Deduplication shares the same pending record as PLAYER_DIED.
    EnshwickPlayerDeathData death={.header=HEADER(EnshwickPlayerDeathData),.entity=entity,.key=player.key,.cause=UINT32_MAX};
    respawn_event(EnshwickEvent_PLAYER_DIED,&death,0);
}
static int respawn_view(const RespawnPool *pool,const void *pointer,uint32_t *offset,uint32_t *current,uint32_t *maximum) {
    if(!pointer)return 0;
    const uint8_t *p=pointer;
    if(little(p,4)!=pool->root || !(p[stamina_layout.flags]&1))return 0;
    uint32_t start=(uint32_t)little(p+stamina_layout.data,2),count=p[stamina_layout.count];
    if(pool->maximum>=count || !range_ok(start,(uint64_t)count*4,pool->size))return 0;
    *current=(uint32_t)little(p+start,4);*maximum=(uint32_t)little(p+start+pool->maximum*4,4);
    if(*maximum>INT32_MAX)return 0;
    *offset=start;return 1;
}
static void respawn_store(uint8_t *p,uint32_t value){for(unsigned b=0;b<4;++b)p[b]=(uint8_t)(value>>(8*b));}
static void respawn_wait(const EnshwickTickData *tick,uint32_t entity,const char *why,int heartbeat){
    if(!heartbeat)return;
    Line l={{0},0};prefix(&l,tick);text(&l," respawn_wait entity=");number(&l,entity);
    text(&l," reason=");text(&l,why);emit(&l);
}
static void tick_respawn(const EnshwickTickData *tick,const EnshwickPlayerInfo *players,uint32_t count,int heartbeat) {
    if(!respawn_ready)return;
    uint64_t now=sprint_clock();
    for(unsigned n=0;n<PLAYERS;++n){RespawnPending *entry=&respawns[n];if(!entry->pending)continue;
        if(entry->expires && now>entry->expires){entry->pending=0;diagnostic_status("respawn_expired",EnshwickStatus_REFUSED);continue;}
        const EnshwickPlayerInfo *player=0;
        for(uint32_t i=0;i<count;++i)if(players[i].key==entry->key && players[i].entity){player=&players[i];break;}
        if(!player){entry->stable=0;respawn_wait(tick,entry->dead_entity,"character_missing",heartbeat);continue;}
        const void *p=0;uint32_t offset[3],current[3],maximum[3];int valid=1;
        for(unsigned k=0;k<3;++k){
            if(api->component(self,player->entity,respawn_pools[k].hash,&p)!=EnshwickStatus_OK ||
               !respawn_view(&respawn_pools[k],p,&offset[k],&current[k],&maximum[k])){valid=0;break;}
        }
        // A corpse must stay dead: the game, not this mod, brings it back.
        if(!valid || !maximum[0] || !current[0] || current[0]>INT32_MAX){entry->stable=0;
            respawn_wait(tick,player->entity,valid?"dead_health":"resources_unavailable",heartbeat);continue;}
        if(api->component(self,player->entity,respawn_actor.hash,&p)!=EnshwickStatus_OK || !p){entry->stable=0;
            respawn_wait(tick,player->entity,"actor_not_alive",heartbeat);continue;}
        uint8_t mode=((const uint8_t *)p)[respawn_actor.offset];
        if(mode==5 || mode==9 || mode>10){entry->stable=0;
            respawn_wait(tick,player->entity,"actor_not_alive",heartbeat);continue;} // Death / DivingDeath.
        int same=entry->entity==player->entity;
        if(!entry->expires)entry->expires=now+UINT64_C(10000);
        for(unsigned k=0;k<3;++k)if(entry->maxima[k]!=maximum[k])same=0;
        entry->entity=player->entity;
        for(unsigned k=0;k<3;++k)entry->maxima[k]=maximum[k];
        if(!same || !entry->stable){entry->stable=1;continue;}
        // Preflight every writable view before changing ANY resource.
        void *write[3]={0};EnshwickStatus status=EnshwickStatus_OK;
        for(unsigned k=0;k<3;++k){
            status=api->component_write(self,player->entity,respawn_pools[k].hash,&write[k]);
            uint32_t live_max=0;
            if(status!=EnshwickStatus_OK)break;
            if(!respawn_view(&respawn_pools[k],write[k],&offset[k],&current[k],&live_max) || live_max!=maximum[k]){
                status=EnshwickStatus_REFUSED;break;}
        }
        if(status!=EnshwickStatus_OK){entry->stable=0;if(heartbeat)diagnostic_status("respawn_write",status);continue;}
        if(!current[0] || current[0]>INT32_MAX){entry->stable=0;continue;}
        for(unsigned k=0;k<3;++k)respawn_store((uint8_t *)write[k]+offset[k],maximum[k]);
        entry->pending=0;entry->stable=0;
        Line l={{0},0};prefix(&l,tick);text(&l," respawn_refill slot=");number(&l,player->slot);
        text(&l," entity=");number(&l,player->entity);
        static const char *const names[]={" health="," stamina="," mana="};
        for(unsigned k=0;k<3;++k){text(&l,names[k]);signed32(&l,current[k]);text(&l,"->");number(&l,maximum[k]);}emit(&l);
    }
}
static void init_respawn(const EnshwickBuildInfo *build) {
    respawn_ready=0;reset_respawn();
    if(!reviewed_client_build(build) || !stamina_layout.available || !api->subscribe || !api->component_write){
        diagnostic_status("respawn_guard",EnshwickStatus_REFUSED);return;}
    const char *const types[]={"keen::ecs::Health","keen::ecs::Stamina","keen::ecs::Mana"};
    const char *const roots[]={"attribute.health","attribute.stamina","attribute.mana"};
    const char *const maxima[]={"attribute.health_max_slot","attribute.stamina_max_slot","attribute.mana_max_slot"};
    EnshwickStatus status=EnshwickStatus_OK;
    for(unsigned k=0;k<3 && status==EnshwickStatus_OK;++k){RespawnPool *pool=&respawn_pools[k];uint64_t root=0,max=0;
        status=api->type_hash(str(types[k]),&pool->hash);
        if(status==EnshwickStatus_OK)status=api->type_size(str(types[k]),&pool->size);
        if(status==EnshwickStatus_OK)status=api->constant(self,str(roots[k]),&root);
        if(status==EnshwickStatus_OK)status=api->constant(self,str(maxima[k]),&max);
        if(status==EnshwickStatus_OK && (!root || !max || root>UINT32_MAX || max>=256 || pool->size<8 ||
           !range_ok(stamina_layout.data,2,pool->size) || !range_ok(stamina_layout.flags,1,pool->size) ||
           !range_ok(stamina_layout.count,1,pool->size)))status=EnshwickStatus_REFUSED;
        pool->root=(uint32_t)root;pool->maximum=(uint32_t)max;
    }
    respawn_actor=resolve("keen::ecs::Actor","currentColliderMode",1,1);
    if(!respawn_actor.available)status=EnshwickStatus_REFUSED;
    if(status==EnshwickStatus_OK)status=api->subscribe(self,EnshwickEvent_PLAYER_LEFT,respawn_event,0);
    if(status==EnshwickStatus_OK)status=api->subscribe(self,EnshwickEvent_PLAYER_DIED,respawn_event,0);
    respawn_ready=status==EnshwickStatus_OK;diagnostic_status("respawn_guard",status);
}
