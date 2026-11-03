#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../probe/sprint_stub.h"

static unsigned checks, calls;
static uintptr_t seen_pointer;
static uint32_t seen_selector;
static int permit;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static int __attribute__((ms_abi)) predicate(uintptr_t pointer,uint32_t selector) {
    ++calls;seen_pointer=pointer;seen_selector=selector;
    // The compiler must preserve the Windows nonvolatile XMM6 used by the game.
    return permit && selector==4;
}
int main(void) {
    uint8_t *code=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(code!=MAP_FAILED);
    // SysV wrapper: RDI=accumulator, ESI=selector, XMM0=cost.
    // Preserve SysV R14, align RSP to 16 at the patch site, set game registers.
    const uint8_t wrapper[]={0x41,0x56,0x48,0x89,0xf8,0x41,0x89,0xf6,0x0f,0x28,0xf0};
    memcpy(code,wrapper,sizeof(wrapper));
    const uintptr_t resume=(uintptr_t)(code+1024);
    code[1024]=0x41;code[1025]=0x5e;code[1026]=0xc3;
    CHECK(build_sprint_stub(code+sizeof(wrapper),512,(uintptr_t)(code+sizeof(wrapper)),(uintptr_t)predicate,resume)>0);
    CHECK(mprotect(code,4096,PROT_READ|PROT_EXEC)==0);
    void (*run)(float *,uint32_t,float);memcpy(&run,&code,sizeof(run));
    float accumulator=7;
    permit=0;run(&accumulator,4,3);CHECK(accumulator==10);
    CHECK(seen_pointer==(uintptr_t)&accumulator && seen_selector==4 && calls==1);
    permit=1;run(&accumulator,4,9);CHECK(accumulator==10);
    run(&accumulator,9,2);CHECK(accumulator==12 && seen_selector==9 && calls==3);
    for(unsigned i=0;i<10000;++i){permit=i%2;run(&accumulator,4,1);}
    CHECK(accumulator==5012 && calls==10003);
    uint8_t tiny[512];
    for(size_t n=0;n<128;++n)CHECK(!build_sprint_stub(tiny,n,0x1000,0x2000,0x3000));
    CHECK(!build_sprint_stub(tiny,sizeof(tiny),0x1000,0x2000,UINT64_C(0x900000000)));
    int32_t delta;
    CHECK(stub_rel32(0x80000000,0,&delta) && delta==INT32_MIN);
    CHECK(!stub_rel32(0x80000001,0,&delta));
    CHECK(stub_rel32(0,INT32_MAX,&delta) && delta==INT32_MAX);
    CHECK(!stub_rel32(0,UINT64_C(0x80000000),&delta));
    CHECK(munmap(code,4096)==0);
    printf("Executable sprint gate tests: %u checks passed\n",checks);
    return 0;
}
