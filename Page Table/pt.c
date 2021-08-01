#include "os.h"

uint32_t get_key_by_level(int level, uint32_t vpn){
    return (level == 0) ? (vpn >> 10) : (vpn & 0x3ff);
}

int is_valid(uint32_t pte){
    return pte & 0x1;
}

void update_pte(uint32_t* pte, uint32_t next_ppn){
    /* valid <-- True, update next_ppn field */
    *pte = (next_ppn << 12) | 0x1;
}

void invalidate_pte(uint32_t* pte) {
    *pte = *pte & 0xffffffe; /* change lsb to 0 */
}


void page_table_update(uint32_t pt, uint32_t vpn, uint32_t ppn){
    uint32_t node1_ppn;
    uint32_t *node0_ptr, *pte0_ptr, *node1_ptr, *pte1_ptr;
    /* level 0 */
    node0_ptr = phys_to_virt(pt << 12);
    pte0_ptr = node0_ptr + (int)get_key_by_level(0,vpn);
    if(! is_valid(*pte0_ptr)){
        if(ppn == NO_MAPPING){
            return;
        }
        else{  /* create new level 1 */
            node1_ppn = alloc_page_frame();
            update_pte(pte0_ptr, node1_ppn);
        }
    }
    node1_ptr = phys_to_virt((*pte0_ptr) & 0xfffff000);
    pte1_ptr = node1_ptr + (int)get_key_by_level(1,vpn);
    if(ppn == NO_MAPPING) {
        invalidate_pte(pte1_ptr);
        return;
    }
    else{
        update_pte(pte1_ptr, ppn);
    }
}


uint32_t page_table_query(uint32_t pt, uint32_t vpn){
    uint32_t *node0_ptr, *pte0_ptr, *node1_ptr, *pte1_ptr;
    /* level 0 */
    node0_ptr = phys_to_virt(pt << 12);
    pte0_ptr = node0_ptr + (int)get_key_by_level(0,vpn);
    if(! is_valid(*pte0_ptr))
        return NO_MAPPING;
    /* level 1 */
    node1_ptr = phys_to_virt((*pte0_ptr) & 0xfffff000);
    pte1_ptr = node1_ptr + (int)get_key_by_level(1,vpn);
    if(! is_valid(*pte1_ptr))
        return NO_MAPPING;
    return *pte1_ptr >> 12;
}
