from mupen_util import *

def analyzeFn(c, pc):
    ra_offset = None
    sp_offset = None
    starting_pc = pc
    while True:
        instr = c.read_u32(pc)
        # Look for sw ra, ___(sp)
        if instr & 0xFFFF0000 == 0xAFBF0000:
            ra_offset = instr & 0xFFFF
        elif instr & 0xFFFF0000 == 0x27BD0000:
            sp_offset = asS16(instr & 0xFFFF)
        elif pc <= 0 or instr == 0x03e00008:
            # Hit a 'jr ra', stop
            break
        if ra_offset is not None and sp_offset is not None:
            break
        if starting_pc - pc >= 16384:
            print("Failed to find function prologue starting at {:08X}".format(starting_pc))
            break
        pc -= 4
    return ra_offset, sp_offset

def getStackPCs(c):
    pcs = [c.pc]

    pc = u32(c.regs[RA])
    sp = u32(c.regs[SP])    
    leaf_ra, leaf_sp = analyzeFn(c, c.pc)
    if leaf_ra is not None:
        pc = c.read_u32(sp + leaf_ra)
    if leaf_sp is not None:
        sp -= leaf_sp

    while len(pcs) < 512:
        pcs.append(pc)
        ra_offset, sp_offset = analyzeFn(c, pc)
        if ra_offset is None or sp_offset is None:
            break

        pc = c.read_u32(sp + ra_offset) - 8
        sp -= sp_offset
    if len(pcs) >= 512:
        print("WARNING: Hit soft stack limit")
    return pcs

    
def backtrace(c):
    pcs = getStackPCs(c)
    print("PC: 0x{:08X}".format(pcs[0]))
    for pc in pcs:
        print("<- 0x{:08X}".format(pc))
