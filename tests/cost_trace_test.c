#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "../probe/probe.c"

static unsigned checks, hooks, originals;
static int32_t meter=100000;
static uint8_t image_code[sizeof(cost_prologue)];
static uint64_t range_size, return_address;
static char output[131072];
static size_t used;
static const void *expected_registry;
static _Atomic(unsigned) stress_forwards;
static _Atomic(int) stress_sum;
static void stress_original(void *attribute,const void *registry,int32_t delta,uint32_t root) {
    (void)attribute;(void)registry;(void)root;
    atomic_fetch_add(&stress_forwards,1);atomic_fetch_add(&stress_sum,delta);
}
static void *producer(void *argument) {
    (void)argument;
    for(unsigned i=0;i<2000;++i)cost_observe(&meter,expected_registry,-1,COST_ROOT);
    return 0;
}
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void original(void *attribute,const void *registry,int32_t delta,uint32_t root) {
    CHECK(attribute==&meter && registry==expected_registry);
    (void)root;meter+=delta;++originals;
}
static void log_line(EnshwickModHandle me,EnshwickLevel level,EnshwickStr message) {
    (void)me;(void)level;CHECK(used+message.len+2<sizeof(output));
    memcpy(output+used,message.ptr,message.len);used+=message.len;output[used++]='\n';output[used]=0;
}
static EnshwickStatus code_range(uint64_t *begin,uint64_t *size) {
    *begin=(uintptr_t)image_code;*size=range_size;return EnshwickStatus_OK;
}
static void frame(EnshwickCallerFrame *out) { out->rsp=(uintptr_t)&return_address; }
static EnshwickStatus install_hook(EnshwickModHandle me,const EnshwickHookSpec *spec) {
    (void)me;CHECK(spec->target==(uintptr_t)image_code);
    CHECK(spec->flags==EnshwickHookFlags_CALLER_FRAME);
    CHECK(spec->next==(const uintptr_t *)&cost_next);
    atomic_store(&cost_next,(uintptr_t)original);++hooks;return EnshwickStatus_OK;
}
static EnshwickStatus component(EnshwickModHandle me,EnshwickEntity entity,uint32_t hash,const void **out) {
    (void)me;CHECK(entity==1 && hash==10);*out=&meter;return EnshwickStatus_OK;
}
int main(void) {
    EnshwickHostApi fake={0};
    fake.log=log_line;fake.hook=install_hook;fake.code_range=code_range;
    fake.caller_frame=frame;fake.component=component;api=&fake;self=1;
    memcpy(image_code,cost_prologue,sizeof(image_code));range_size=sizeof(image_code);
    EnshwickBuildInfo build={.header=HEADER(EnshwickBuildInfo),.side=EnshwickSide_CLIENT,
                            .table=EnshwickBuildTable_EXACT};
    build.base=(uintptr_t)image_code-COST_RVA;build.fingerprint.age=520;
    memcpy(build.fingerprint.guid,cost_guid,16);
    build.fingerprint.age=999;init_cost_trace(&build);CHECK(hooks==0);
    build.fingerprint.age=520;image_code[0]^=1;init_cost_trace(&build);CHECK(hooks==0);
    image_code[0]^=1;range_size=1;init_cost_trace(&build);CHECK(hooks==0);
    range_size=sizeof(image_code);init_cost_trace(&build);CHECK(hooks==1);
    CHECK(strstr(output,"cost_trace_hook status=0")!=0);
    // A reviewed PATTERNS build is permitted explicitly; a changed identity,
    // side, table mode, or the old build in PATTERNS mode must stay refused.
    CHECK(reviewed_client_build(&build)==1);
    build.table=EnshwickBuildTable_PATTERNS;CHECK(!reviewed_client_build(&build));
    memcpy(build.fingerprint.guid,client_205_guid,16);build.fingerprint.age=205;
    CHECK(reviewed_client_build(&build)==2);
    init_cost_trace(&build);CHECK(hooks==2);
    build.fingerprint.guid[0]^=1;init_cost_trace(&build);CHECK(hooks==2);
    build.fingerprint.guid[0]^=1;build.fingerprint.age=206;CHECK(!reviewed_client_build(&build));
    build.fingerprint.age=205;build.side=EnshwickSide_SERVER;CHECK(!reviewed_client_build(&build));
    build.side=EnshwickSide_CLIENT;build.table=99;CHECK(!reviewed_client_build(&build));
    build.table=EnshwickBuildTable_EXACT;CHECK(reviewed_client_build(&build)==2);
    CHECK(sprint_code_digest((const uint8_t *)"hello",5)==UINT32_C(0x4f9f2cab));
    uint8_t unreviewed_body[0x5b3]={0};
    CHECK(!reviewed_sprint_code(1,unreviewed_body,sizeof(unreviewed_body)));
    CHECK(!reviewed_sprint_code(2,unreviewed_body,sizeof(unreviewed_body)));
    CHECK(!reviewed_sprint_code(0,unreviewed_body,sizeof(unreviewed_body)));
    CHECK(!reviewed_sprint_code(2,unreviewed_body,sizeof(unreviewed_body)-1));
    return_address=(uintptr_t)image_code+4;
    int registry=42;expected_registry=&registry;
    cost_observe(&meter,&registry,-7,COST_ROOT);
    CHECK(meter==99993 && originals==1 && cost_count==1);
    CHECK(cost_queue[0].delta==-7 && cost_queue[0].caller_rva==COST_RVA+4);
    cost_observe(&meter,&registry,3,COST_ROOT);
    cost_observe(&meter,&registry,-2,0x1234);
    CHECK(originals==3 && cost_count==1 && meter==99994);
    stamina_layout.available=1;stamina_layout.hash=10;
    EnshwickPlayerInfo player={.header=HEADER(EnshwickPlayerInfo),.entity=1,.slot=0};
    EnshwickTickData tick_data={.header=HEADER(EnshwickTickData),.game=1,.step=60};
    drain_cost_trace(&tick_data,&player,1);
    CHECK(cost_count==0 && strstr(output,"cost_trace entity=1 delta=-7")!=0);
    CHECK(strstr(output,"forwarded_unchanged=true")!=0);
    for(unsigned i=0;i<COST_CAPACITY+1;++i)cost_observe(&meter,&registry,-1,COST_ROOT);
    CHECK(cost_count==COST_CAPACITY && atomic_load(&cost_dropped)==1);
    drain_cost_trace(&tick_data,&player,1);
    CHECK(strstr(output,"dropped=1")!=0 && atomic_load(&cost_dropped)==0);
    atomic_flag_test_and_set(&cost_lock);
    unsigned before=originals;cost_observe(&meter,&registry,-1,COST_ROOT);
    CHECK(originals==before+1 && atomic_load(&cost_dropped)==1);
    atomic_flag_clear(&cost_lock);
    return_address=1;cost_observe(&meter,&registry,-1,COST_ROOT);
    CHECK(cost_queue[0].caller_rva==0);
    reset_cost_trace();CHECK(cost_count==0);
    atomic_store(&cost_dropped,0);
    atomic_store(&cost_next,(uintptr_t)stress_original);
    pthread_t threads[4];
    for(unsigned i=0;i<4;++i)CHECK(pthread_create(&threads[i],0,producer,0)==0);
    for(unsigned i=0;i<4;++i)CHECK(pthread_join(threads[i],0)==0);
    CHECK(atomic_load(&stress_forwards)==8000 && atomic_load(&stress_sum)==-8000);
    CHECK(cost_count+atomic_load(&cost_dropped)==8000);
    CHECK(cost_count<=COST_CAPACITY);
    atomic_store(&cost_next,(uintptr_t)original);
    // /210 uses the same GUID but a separately reviewed age and moved helper.
    build.fingerprint.age=210;build.table=EnshwickBuildTable_PATTERNS;
    build.base=(uintptr_t)image_code-(COST_RVA+UINT64_C(0xe0));
    CHECK(reviewed_client_build(&build)==3 && reviewed_code_shift(3)==0xe0);
    CHECK(reviewed_code_shift(1)==0 && reviewed_code_shift(2)==0);
    image_code[0]^=1;init_cost_trace(&build);CHECK(hooks==2);image_code[0]^=1;
    init_cost_trace(&build);CHECK(hooks==3);
    reset_cost_trace();return_address=(uintptr_t)image_code+4;
    cost_observe(&meter,&registry,-1,COST_ROOT);
    CHECK(cost_count==1 && cost_queue[0].caller_rva==COST_RVA+0xe0+4);
    build.fingerprint.age=211;init_cost_trace(&build);CHECK(hooks==3);
    build.fingerprint.age=210;build.fingerprint.guid[0]^=1;CHECK(!reviewed_client_build(&build));
    printf("Cost observation tests: %u checks passed\n",checks);
    return 0;
}
