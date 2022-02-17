struct r4300_core;

#ifdef __cplusplus
extern "C" {
#endif

void pyLoadHooks(const char *path);
void pyRunPCHooks(struct r4300_core* r4300);
void pyRunReadHooks(struct r4300_core* r4300, uint32_t address);
void pyRunWriteHooks(struct r4300_core* r4300, uint32_t address, uint64_t value, uint64_t mask);

#ifdef __cplusplus
}
#endif
