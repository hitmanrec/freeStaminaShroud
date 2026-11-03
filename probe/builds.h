// Explicitly reviewed client builds. PATTERNS is allowed only for the second
// fingerprint, whose submitted report was checked; never accept it globally.
static const uint8_t cost_guid[] = {
    0x69,0xf0,0xe9,0x57,0x3b,0x32,0xc2,0x4a,0x88,0x38,0xd3,0xff,0xf5,0x50,0x82,0x4c
};
static const uint8_t client_205_guid[] = {
    0xe3,0x78,0xda,0x3d,0x60,0x79,0xc0,0x49,0xb3,0x0c,0xd1,0x60,0x4a,0x0e,0x78,0x46
};
static unsigned reviewed_client_build(const EnshwickBuildInfo *build) {
    if(build->side!=EnshwickSide_CLIENT)return 0;
    int old=build->fingerprint.age==520 && build->table==EnshwickBuildTable_EXACT;
    int current=build->fingerprint.age==205 &&
        (build->table==EnshwickBuildTable_EXACT || build->table==EnshwickBuildTable_PATTERNS);
    for(unsigned i=0;i<16;++i){
        if(build->fingerprint.guid[i]!=cost_guid[i])old=0;
        if(build->fingerprint.guid[i]!=client_205_guid[i])current=0;
    }
    return old?1:current?2:0;
}
static uint32_t sprint_code_digest(const uint8_t *code,size_t length) {
    uint32_t h=UINT32_C(2166136261);
    for(size_t i=0;i<length;++i)h=(h^code[i])*UINT32_C(16777619);
    return h;
}
static int reviewed_sprint_code(unsigned build,const uint8_t *code,size_t length) {
    if(length!=0x5b3 || (build!=1 && build!=2))return 0;
    uint32_t expected=build==1?UINT32_C(0xc01aa88c):UINT32_C(0x40862718);
    return sprint_code_digest(code,length)==expected;
}
