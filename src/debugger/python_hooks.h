struct r4300_core;

#ifdef __cplusplus
extern "C" {
#endif

void pyLoadHooks(const char *path);
void pyRunPCHooks(struct r4300_core* r4300);
void pyRunButtonHooks(struct r4300_core* r4300);
void pyRunRamReadHooks(struct r4300_core* r4300, uint32_t address);
void pyRunRamWriteHooks(struct r4300_core* r4300, uint32_t address, uint64_t value, uint64_t mask);
void pyRunCartReadHooks(struct r4300_core* r4300, uint32_t base, uint32_t len, uint32_t dst);
void pyRunCartWriteHooks(struct r4300_core* r4300, uint32_t base, uint32_t len, uint32_t dst);

extern char g_run_button_hooks;

#ifdef __cplusplus
}
#endif
