/*
 * Copyright (c) 2011-2014 Wind River Systems, Inc.
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel.h>
#include <mmustructs.h>
#include <linker/linker-defs.h>

/* Common regions for all x86 processors.
 * Peripheral I/O ranges configured at the SOC level
 */

/* Mark text and rodata as read-only.
 * Userspace may read all text and rodata.
 */
MMU_BOOT_REGION((u32_t)&_image_rom_start, (u32_t)&_image_rom_size,
		MMU_ENTRY_READ | MMU_ENTRY_USER);

#ifdef CONFIG_APPLICATION_MEMORY
/* User threads by default can read/write app-level memory. */
MMU_BOOT_REGION((u32_t)&__app_ram_start, (u32_t)&__app_ram_size,
		MMU_ENTRY_WRITE | MMU_ENTRY_USER);
#endif

/* __kernel_ram_size includes all unused memory, which is used for heaps.
 * User threads cannot access this unless granted at runtime. This is done
 * automatically for stacks.
 */
MMU_BOOT_REGION((u32_t)&__kernel_ram_start, (u32_t)&__kernel_ram_size,
		MMU_ENTRY_WRITE | MMU_ENTRY_RUNTIME_USER);


void _x86_mmu_get_flags(void *addr, u32_t *pde_flags, u32_t *pte_flags)
{

	*pde_flags = X86_MMU_GET_PDE(addr)->value & ~MMU_PDE_PAGE_TABLE_MASK;
	*pte_flags = X86_MMU_GET_PTE(addr)->value & ~MMU_PTE_PAGE_MASK;
}


int _arch_buffer_validate(void *addr, size_t size, int write)
{
	u32_t start_pde_num;
	u32_t end_pde_num;
	u32_t starting_pte_num;
	u32_t ending_pte_num;
	struct x86_mmu_page_table *pte_address;
	u32_t pde;
	u32_t pte;
	union x86_mmu_pte pte_value;

	start_pde_num = MMU_PDE_NUM(addr);
	end_pde_num = MMU_PDE_NUM((char *)addr + size - 1);
	starting_pte_num = MMU_PAGE_NUM((char *)addr);

	/* Iterate for all the pde's the buffer might take up.
	 * (depends on the size of the buffer and start address of the buff)
	 */
	for (pde = start_pde_num; pde <= end_pde_num; pde++) {
		union x86_mmu_pde_pt pde_value = X86_MMU_PD->entry[pde].pt;

		if (!pde_value.p || !pde_value.us || (write && !pde_value.rw)) {
			return -EPERM;
		}

		pte_address = X86_MMU_GET_PT_ADDR(addr);

		/* loop over all the possible page tables for the required
		 * size. If the pde is not the last one then the last pte
		 * would be 1023. So each pde will be using all the
		 * page table entires except for the last pde.
		 * For the last pde, pte is calculated using the last
		 * memory address of the buffer.
		 */
		if (pde != end_pde_num) {
			ending_pte_num = 1023;
		} else {
			ending_pte_num = MMU_PAGE_NUM((char *)addr + size - 1);
		}

		/* For all the pde's appart from the starting pde, will have
		 * the start pte number as zero.
		 */
		if (pde != start_pde_num) {
			starting_pte_num = 0;
		}

		pte_value.value = 0xFFFFFFFF;

		/* Bitwise AND all the pte values. */
		for (pte = starting_pte_num; pte <= ending_pte_num; pte++) {
			pte_value.value &= pte_address->entry[pte].value;
		}

		if (!pte_value.p || !pte_value.us || (write && !pte_value.rw)) {
			return -EPERM;
		}
	}

	return 0;
}


static inline void tlb_flush_page(void *addr)
{
	/* Invalidate TLB entries corresponding to the page containing the
	 * specified address
	 */
	char *page = (char *)addr;
	__asm__ ("invlpg %0" :: "m" (*page));
}


void _x86_mmu_set_flags(void *ptr, size_t size, u32_t flags, u32_t mask)
{
	union x86_mmu_pte *pte;

	u32_t addr = (u32_t)ptr;

	__ASSERT(!(addr & MMU_PAGE_MASK), "unaligned address provided");
	__ASSERT(!(size & MMU_PAGE_MASK), "unaligned size provided");

	while (size) {

		/* TODO we're not generating 4MB entries at the moment */
		__ASSERT(X86_MMU_GET_4MB_PDE(addr)->ps != 1, "4MB PDE found");

		pte = X86_MMU_GET_PTE(addr);

		pte->value = (pte->value & ~mask) | flags;
		tlb_flush_page((void *)addr);

		size -= MMU_PAGE_SIZE;
		addr += MMU_PAGE_SIZE;
	}
}
