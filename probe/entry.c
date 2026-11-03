// No CRT startup or work under the loader lock. Enshwick calls our init later.
#if defined(_WIN32)
int __stdcall probe_entry(void *module, unsigned long reason, void *reserved) {
    (void)module; (void)reason; (void)reserved;
    return 1;
}
#endif
