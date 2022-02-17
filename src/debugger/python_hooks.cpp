#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

#include <pybind11/numpy.h>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include "python_hooks.h"

extern "C" {
#include "device/r4300/r4300_core.h"
#include "device/rdram/rdram.h"
}

namespace py = pybind11;
using namespace pybind11::literals; 

PYBIND11_MAKE_OPAQUE(std::vector<uint64_t>);


static uint32_t nextCookie;

struct Hook {
    py::function callback;
    uint32_t cookie;
};

struct RangeHook {
    uint32_t min;
    uint32_t max;
    py::function callback;
    uint32_t cookie;
};

static std::map<uint32_t, std::vector<Hook> > all_pc_hooks;

static std::vector<RangeHook> all_ram_read_hooks;
static std::vector<RangeHook> all_ram_write_hooks;

// TODO: mark hooks for removal rather than doing it automatically
// TODO: accept a True/False return value from hook that determines
//       whether to delete it or not

uint32_t registerPCHook(uint32_t pc, py::function callback) {
    auto pc_hooks = all_pc_hooks[pc];
    pc_hooks.push_back({callback, nextCookie});
    all_pc_hooks[pc] = pc_hooks;
    nextCookie += 1;
    printf("Registered hook %s at PC 0x%08X\n", std::string(py::str(callback.attr("__name__"))).c_str(), pc);
    return nextCookie - 1;
}

void removePCHook(uint32_t cookie) {
    for (auto &pair : all_pc_hooks) {
        for (auto it = pair.second.begin(); it != pair.second.end(); it++) {
            if (it->cookie == cookie) {
                printf("Removed hook %s at PC 0x%08X\n", std::string(py::str(it->callback.attr("__name__"))).c_str(), pair.first);
                pair.second.erase(it);
                if (pair.second.size() == 0) {
                    all_pc_hooks.erase(pair.first);
                } else {
                    all_pc_hooks[pair.first] = pair.second;
                }
                return;
            }        
        }
    }
}

uint32_t registerRAMReadHook(uint32_t addr_min, uint32_t addr_max, py::function callback) {
    all_ram_read_hooks.push_back({addr_min, addr_max, callback, nextCookie});
    nextCookie += 1;
    printf("Registered hook %s for reads in range [0x%08X - 0x%08X) \n", std::string(py::str(callback.attr("__name__"))).c_str(), addr_min, addr_max);
    return nextCookie - 1;
}


void removeRAMReadHook(uint32_t cookie) {
    for (auto it = all_ram_read_hooks.begin(); it != all_ram_read_hooks.end(); it++) {
        if (it->cookie == cookie) {
            printf("Removed hook %s for reads in range [0x%08X - 0x%08X)\n", std::string(py::str(it->callback.attr("__name__"))).c_str(), it->min, it->max);
            all_ram_read_hooks.erase(it);
            return;
        }        
    }
}

uint32_t registerRAMWriteHook(uint32_t addr_min, uint32_t addr_max, py::function callback) {
    all_ram_write_hooks.push_back({addr_min, addr_max, callback, nextCookie});
    nextCookie += 1;
    printf("Registered hook %s for writes in range [0x%08X - 0x%08X) \n", std::string(py::str(callback.attr("__name__"))).c_str(), addr_min, addr_max);
    return nextCookie - 1;
}

void removeRAMWriteHook(uint32_t cookie) {
    for (auto it = all_ram_write_hooks.begin(); it != all_ram_write_hooks.end(); it++) {
        if (it->cookie == cookie) {
            printf("Removed hook %s for reads in range [0x%08X - 0x%08X)\n", std::string(py::str(it->callback.attr("__name__"))).c_str(), it->min, it->max);
            all_ram_write_hooks.erase(it);
            return;
        }        
    }
}


class CoreState {
    public:
        CoreState(struct r4300_core* r4300) {
            this->r4300 = r4300;
            pc = r4300->interp_PC.addr;
            regs.reserve(32);
            regs.assign(r4300->regs, r4300->regs + 32);

            hi = r4300->hi;
            lo = r4300->lo;
        }

        void commit() {
            r4300->interp_PC.addr = pc;
            memcpy(r4300->regs, &regs[0], 32 * sizeof(uint64_t));
            r4300->hi = hi;
            r4300->lo = lo;
        }

        uint32_t pc;
        std::vector<uint64_t> regs;
        int64_t hi;
        int64_t lo;

        uint32_t read_u32(uint32_t address) {
            uint32_t alignment = address & 3;
            if (alignment == 0) {
                uint32_t value;
                _untracked_r4300_read_aligned_word(this->r4300, address, &value, "debug");
                return value;
            } else {
                uint32_t address_left = address & 0xFFFFFFFC;
                uint32_t address_right = address_left + 4;
                uint32_t value_left;
                uint32_t value_right;
                _untracked_r4300_read_aligned_word(this->r4300, address_left, &value_left, "debug");
                _untracked_r4300_read_aligned_word(this->r4300, address_right, &value_right, "debug");
                return (value_left << (alignment * 8)) |
                       (value_right >> (32 - alignment * 8));
            }
        }

        uint16_t read_u16(uint32_t address) {
           return (read_u32(address) & 0xFFFF0000) >> 16;
        }

        uint8_t read_u8(uint32_t address) {
           return (read_u32(address) & 0xFF000000) >> 24;
        }

        void write_u32(uint32_t address, uint32_t value, uint32_t mask=0xFFFFFFFF) {
            uint32_t alignment = address & 3;
            if (alignment == 0) {
                _untracked_r4300_write_aligned_word(this->r4300, address, value, mask);
            } else {
                // TODO: test this
                uint32_t address_left = address & 0xFFFFFFFC;
                uint32_t address_right = address_left + 4;
                uint32_t mask_left = (alignment == 1) ? 0x000000FF : (alignment == 2) ? 0x0000FFFF : 0x00FFFFFF;
                uint32_t mask_right = (alignment == 1) ? 0xFFFFFF00 : (alignment == 2) ? 0xFFFF0000 : 0xFF000000;
                uint32_t value_left = (value >> (32 - alignment * 8)) & mask_left;
                uint32_t value_right = (value << (alignment * 8)) & mask_right;
                mask_left &= (mask >> (32 - alignment * 8));
                mask_right &= (mask << (alignment * 8));
                _untracked_r4300_write_aligned_word(this->r4300, address_left, value_left, mask_left);
                _untracked_r4300_write_aligned_word(this->r4300, address_right, value_right, mask_right);
            }
        }

        void write_u16(uint32_t address, uint16_t value, uint16_t mask=0xFFFF) {
            write_u32(address, value, mask);
        }

        void write_u8(uint32_t address, uint8_t value, uint8_t mask=0xFF) {
            write_u32(address, value, mask);
        }

        void dump_rdram(const char *filename) {
            size_t start = 0;
            size_t end = this->r4300->rdram->dram_size;

            start /= 4;
            end /= 4;

            FILE *f = fopen(filename, "wb");
            for(uint32_t *cursor = ((uint32_t *)this->r4300->rdram->dram) + start;
                cursor < ((uint32_t *)this->r4300->rdram->dram) + end;
                cursor++)
            {
                uint32_t word = htobe32(*cursor);
                fwrite(&word, 4, 1, f);
            }

            fclose(f);
        }

    private:
        struct r4300_core* r4300;

};

PYBIND11_EMBEDDED_MODULE(mupen_core, m) {
    m.def("registerPCHook", &registerPCHook, "Register a callback for a specific PC address");
    m.def("removePCHook", &removePCHook, "Remove a callback for a specific PC address");

    m.def("registerRAMReadHook", &registerRAMReadHook, "Register a callback for reads within an RDRAM address range");
    m.def("removeRAMReadHook", &removeRAMReadHook, "Remove a callback for reads within an RDRAM address range");

    m.def("registerRAMWriteHook", &registerRAMWriteHook, "Register a callback for writes within an RDRAM address range");
    m.def("removeRAMWriteHook", &removeRAMWriteHook, "Remove a callback for writes within an RDRAM address range");

    py::bind_vector<std::vector<uint64_t>>(m, "RegsVector");

    py::class_<CoreState>(m, "CoreState")
        // .def(py::init<>())
        .def_readwrite("pc", &CoreState::pc)
        .def_readwrite("regs", &CoreState::regs)

        // .def_property_readonly("regs", [](py::object& obj) {
        //      CoreState& o = obj.cast<CoreState&>();
        //      return py::array{32, o.regs, obj};
        //  })
        .def_readwrite("hi", &CoreState::hi)
        .def_readwrite("lo", &CoreState::lo)
        .def("read_u32", &CoreState::read_u32)
        .def("read_u16", &CoreState::read_u16)
        .def("read_u8", &CoreState::read_u8)
        .def("write_u32", &CoreState::write_u32)
        .def("write_u16", &CoreState::write_u16)
        .def("write_u8", &CoreState::write_u8)
        .def("dump_rdram", &CoreState::dump_rdram)
    ;
}


static inline void runRangeHooks(struct r4300_core* r4300, uint32_t address, std::vector<RangeHook>& hooks, uint64_t value, uint64_t mask) {
    if (hooks.size() == 0 || r4300 == NULL) {
        return;
    }

    CoreState state {r4300};

    for (auto hook : hooks) {
        if (address >= hook.min && address < hook.max) {
            hook.callback(&state, address, value, mask);
        }
    }

    state.commit();
}

extern "C" void pyRunReadHooks(struct r4300_core* r4300, uint32_t address) {
    runRangeHooks(r4300, address, all_ram_read_hooks, 0, 0);
}


extern "C" void pyRunWriteHooks(struct r4300_core* r4300, uint32_t address, uint64_t value, uint64_t mask) {
    runRangeHooks(r4300, address, all_ram_write_hooks, value, mask);
}


extern "C" void pyRunPCHooks(struct r4300_core* r4300) {
    if (r4300 == NULL) {
        return;
    }

    uint32_t pc = r4300->interp_PC.addr; // *r4300_pc(r4300);
    auto it = all_pc_hooks.find(pc);
    if (it == all_pc_hooks.end()) {
        return;
    }

    CoreState state {r4300};

    for (auto &hook : it->second){
        hook.callback(&state);
    }

    state.commit();
}

extern "C" void pyLoadHooks(const char *path) {
    printf("Scanning %s for hooks\n", path);
    struct dirent *entry;
    DIR *dp;

    nextCookie = 0;

    std::vector<std::string> hookFiles;
    dp = opendir(path);
    if (dp == NULL) {
        fprintf(stderr, "opendir: Path does not exist or could not be read.\n");
        return;
    }
    while ((entry = readdir(dp))) {
        hookFiles.push_back(std::string(path) + "/" + std::string(entry->d_name));
    }
    closedir(dp);

    // TODO: only run files ending in '.py'
    std::sort(hookFiles.begin(), hookFiles.end());

    // py::scoped_interpreter python_interpreter{};
    py::initialize_interpreter();
    
    auto py_sys = py::module::import("sys");
    py_sys.attr("path").attr("append")(py::str(path));

    auto mupen_core = py::module::import("mupen_core");

    for (auto &filename : hookFiles){
        py::dict scope;
        py::eval_file(filename, scope);
        printf("Imported %s\n", filename.c_str());
    }

}