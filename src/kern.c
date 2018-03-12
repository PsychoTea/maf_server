#include "kern.h"

uint32_t rk32(uint64_t kaddr) {
    kern_return_t err;
    uint32_t val = 0;
    mach_vm_size_t outsize = 0;
    
    kern_return_t mach_vm_write(vm_map_t target_task,
                                mach_vm_address_t address,
                                vm_offset_t data,
                                mach_msg_type_number_t dataCnt);
    
    err = mach_vm_read_overwrite(tfp0,
                                 (mach_vm_address_t)kaddr,
                                 (mach_vm_size_t)sizeof(uint32_t),
                                 (mach_vm_address_t)&val,
                                 &outsize);
    
    if (err != KERN_SUCCESS) {
        return 0;
    }
    
    if (outsize != sizeof(uint32_t)) {
        return 0;
    }
    
    return val;
}

uint64_t rk64(uint64_t kaddr) {
    uint64_t lower = rk32(kaddr);
    uint64_t higher = rk32(kaddr + 4);
    return ((higher << 32) | lower);
}

uint128_t rk128(uint64_t kaddr) {
    uint64_t lower = rk64(kaddr);
    uint64_t higher = rk64(kaddr + 8);
    return (uint128_t){lower, higher};
}

void wk32(uint64_t kaddr, uint32_t val) {
    if (tfp0 == MACH_PORT_NULL) {
        return;
    }
    
    kern_return_t err;
    err = mach_vm_write(tfp0,
                        (mach_vm_address_t)kaddr,
                        (vm_offset_t)&val,
                        (mach_msg_type_number_t)sizeof(uint32_t));
    
    if (err != KERN_SUCCESS) {
        return;
    }
}

void wk64(uint64_t kaddr, uint64_t val) {
    uint32_t lower = (uint32_t)(val & 0xffffffff);
    uint32_t higher = (uint32_t)(val >> 32);
    wk32(kaddr, lower);
    wk32(kaddr + 4, higher);
}

void wk128(uint64_t kaddr, uint128_t val) {
    wk64(kaddr, val.lower);
    wk64(kaddr + 8, val.upper);
}
