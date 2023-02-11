import json
import struct

import mupen_core

AT = 1
V0 = 2
V1 = 3
A0 = 4
A1 = 5
A2 = 6
A3 = 7
T0 = 8
T1 = 9
T2 = 10
T3 = 11
T4 = 12
T5 = 13
T6 = 14
T7 = 15
S0 = 16
S1 = 17
S2 = 18
S3 = 19
S4 = 20
S5 = 21
S6 = 22
S7 = 23
T8 = 24
T9 = 25
GP = 28
SP = 29
FP = 30
S8 = 30
RA = 31

def cstr(c, addr, max_len=None):
    buff = ""
    while True:
        ordval = c.read_u8(addr)
        addr += 1
        if ordval == 0:
            break
        char = chr(ordval)
        buff += char
        if max_len is not None and len(buff) >= max_len:
            break
    return buff


def u32(val):
    return val & 0xFFFFFFFF

# TODO: refactor one_shot code now that we have
#       cookie and unregister attributes in hooks

def pcHook(pc, one_shot=False):
    def decorator(func):
        if one_shot is True:
            cookie = None
            def new_func(*args, **kwargs):
                nonlocal cookie
                mupen_core.removePCHook(cookie)
                return func(*args, **kwargs)
            cookie = mupen_core.registerPCHook(pc, new_func)
            func.cookie = cookie
            return new_func
        else:
            func.cookie = mupen_core.registerPCHook(pc, func)
            func.unregister = lambda: mupen_core.removePCHook(func.cookie)
            return func
    return decorator

def buttonHook(*args):
    button_struct = [
        "R_DPAD",
        "L_DPAD",
        "D_DPAD",
        "U_DPAD",
        "START_BUTTON",
        "Z_TRIG",
        "B_BUTTON",
        "A_BUTTON",

        "R_CBUTTON",
        "L_CBUTTON",
        "D_CBUTTON",
        "U_CBUTTON",
        "R_TRIG",
        "L_TRIG",
    ]
    # TODO: this triggers way more often than it should
    buttons = 0
    for key in args:
        idx = button_struct.index(key.upper())
        buttons |= 1 << idx
    def decorator(func):
        func.cookie = mupen_core.registerButtonHook(buttons, func)
        func.unregister = lambda: mupen_core.removeButtonHook(func.cookie)
        return func
    return decorator

def ramReadHook(addr_min, addr_max=None, one_shot=False):
    if addr_max is None:
        addr_max = addr_min + 1
    def decorator(func):
        if one_shot is True:
            cookie = None
            def new_func(*args, **kwargs):
                nonlocal cookie
                mupen_core.removeRAMReadHook(cookie)
                return func(*args, **kwargs)
            cookie = mupen_core.registerRAMReadHook(addr_min, addr_max, new_func)
            func.cookie = cookie
            return new_func
        else:
            func.cookie = mupen_core.registerRAMReadHook(addr_min, addr_max, func)
            func.unregister = lambda: mupen_core.removeRAMReadHook(func.cookie)
            return func
    return decorator

def ramWriteHook(addr_min, addr_max=None, one_shot=False):
    if addr_max is None:
        addr_max = addr_min + 1
    def decorator(func):
        if one_shot is True:
            cookie = None
            def new_func(*args, **kwargs):
                nonlocal cookie
                mupen_core.removeRAMWriteHook(cookie)
                return func(*args, **kwargs)
            cookie = mupen_core.registerRAMWriteHook(addr_min, addr_max, new_func)
            new_func.cookie = cookie
            return new_func
        else:
            func.cookie = mupen_core.registerRAMWriteHook(addr_min, addr_max, func)
            func.unregister = lambda: mupen_core.removeRAMWriteHook(func.cookie)
            return func
    return decorator

def cartReadHook(addr_min, addr_max=None, one_shot=False):
    if addr_max is None:
        addr_max = addr_min + 1
    def decorator(func):
        if one_shot is True:
            cookie = None
            def new_func(*args, **kwargs):
                nonlocal cookie
                mupen_core.removeCartReadHook(cookie)
                return func(*args, **kwargs)
            cookie = mupen_core.registerRAMReadHook(addr_min, addr_max, new_func)
            func.cookie = cookie
            return new_func
        else:
            func.cookie = mupen_core.registerCartReadHook(addr_min, addr_max, func)
            func.unregister = lambda: mupen_core.removeCartReadHook(func.cookie)
            return func
    return decorator

def cartWriteHook(addr_min, addr_max=None, one_shot=False):
    if addr_max is None:
        addr_max = addr_min + 1
    def decorator(func):
        if one_shot is True:
            cookie = None
            def new_func(*args, **kwargs):
                nonlocal cookie
                mupen_core.removeCartWriteHook(cookie)
                return func(*args, **kwargs)
            cookie = mupen_core.registerCartWriteHook(addr_min, addr_max, new_func)
            new_func.cookie = cookie
            return new_func
        else:
            func.cookie = mupen_core.registerCartWriteHook(addr_min, addr_max, func)
            func.unregister = lambda: mupen_core.removeCartWriteHook(func.cookie)
            return func
    return decorator


def asFloat(raw):
    return struct.unpack(">f", struct.pack(">I", raw))[0]

def floatWord(raw):
    return struct.unpack(">I", struct.pack(">f", raw))[0]

def asS32(raw):
    return struct.unpack(">i", struct.pack(">I", raw))[0]

def asS16(raw):
    return struct.unpack(">h", struct.pack(">H", raw))[0]

def point3D(c, addr):
    return (asFloat(c.read_u32(addr)),
            asFloat(c.read_u32(addr+0x4)),
            asFloat(c.read_u32(addr+0x8)))

def maskedWrite(val, mask):
    val = "{:08X}".format(val)
    mask = "{:08X}".format(mask)
    return "".join("_" if mask[idx]=="0" else val[idx] for idx in range(len(val)))

def dump_regs(core, filename):
    with open(filename, "w") as f:
        f.write(json.dumps({
            "pc": core.pc,
            "gp": list(core.regs)
        }))