// Windows x64 gate at the verified, 16-byte-aligned sprint loop call site.
// Save volatile GPRs, flags and XMM0..5; the ABI preserves XMM6..15.
#include <stddef.h>
#include <stdint.h>
typedef struct { uint8_t *p; size_t used, capacity; int valid; } StubWriter;
static void stub_byte(StubWriter *w,uint8_t b) {
    if(w->used>=w->capacity){w->valid=0;return;}w->p[w->used++]=b;
}
static void stub_data(StubWriter *w,const uint8_t *p,size_t n) {
    for(size_t i=0;i<n;++i)stub_byte(w,p[i]);
}
static void stub_u64(StubWriter *w,uint64_t n) {for(unsigned i=0;i<8;++i)stub_byte(w,(uint8_t)(n>>(i*8)));}
static int stub_rel32(uintptr_t from,uintptr_t to,int32_t *out) {
    if(to>=from){if(to-from>INT32_MAX)return 0;*out=(int32_t)(to-from);}
    else {if(from-to>UINT64_C(2147483648))return 0;*out=(int32_t)(-(int64_t)(from-to));}return 1;
}
static void stub_jump(StubWriter *w,uintptr_t address,uintptr_t target) {
    int32_t delta=0;stub_byte(w,0xe9);
    if(!stub_rel32(address+w->used+4,target,&delta))w->valid=0;
    for(unsigned i=0;i<4;++i)stub_byte(w,(uint8_t)((uint32_t)delta>>(8*i)));
}
static size_t build_sprint_stub(uint8_t *out,size_t capacity,uintptr_t address,
                                uintptr_t predicate,uintptr_t resume) {
    StubWriter w={out,0,capacity,1};
    const uint8_t push[]={0x9c,0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53};
    const uint8_t pop[]={0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0x58,0x9d};
    const uint8_t sub[]={0x48,0x81,0xec,0x80,0,0,0};
    const uint8_t add[]={0x48,0x81,0xc4,0x80,0,0,0};
    stub_data(&w,push,sizeof(push));stub_data(&w,sub,sizeof(sub));
    for(unsigned x=0;x<6;++x){const uint8_t s[]={0xf3,0x0f,0x7f,(uint8_t)(0x44+x*8),0x24,(uint8_t)(32+x*16)};stub_data(&w,s,sizeof(s));}
    const uint8_t args[]={0x48,0x89,0xc1,0x44,0x89,0xf2,0x48,0xb8};
    stub_data(&w,args,sizeof(args));stub_u64(&w,predicate);
    const uint8_t call[]={0xff,0xd0,0x41,0x88,0xc3};stub_data(&w,call,sizeof(call));
    for(unsigned x=0;x<6;++x){const uint8_t s[]={0xf3,0x0f,0x6f,(uint8_t)(0x44+x*8),0x24,(uint8_t)(32+x*16)};stub_data(&w,s,sizeof(s));}
    stub_data(&w,add,sizeof(add));
    const uint8_t test[]={0x45,0x84,0xdb,0x75};stub_data(&w,test,sizeof(test));
    size_t branch=w.used;stub_byte(&w,0);
    stub_data(&w,pop,sizeof(pop));
    const uint8_t original[]={0xf3,0x0f,0x58,0x30,0xf3,0x0f,0x11,0x30};
    stub_data(&w,original,sizeof(original));stub_jump(&w,address,resume);
    if(w.used-branch-1>127)w.valid=0;
    if(branch<capacity)out[branch]=(uint8_t)(w.used-branch-1);
    stub_data(&w,pop,sizeof(pop));stub_jump(&w,address,resume);
    return w.valid?w.used:0;
}
