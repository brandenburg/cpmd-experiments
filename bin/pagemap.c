#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#define PAGEMAP_ENTRY_LEN 8 /* 8 bytes = 64 bits */

/* From Documentation/vm/pagemap.txt:
 *   * Bits 0-54  page frame number (PFN) if present
 *   * Bits 0-4   swap type if swapped
 *   * Bits 5-54  swap offset if swapped
 *   * Bit  55    pte is soft-dirty (see Documentation/vm/soft-dirty.txt)
 *   * Bit  56    page exclusively mapped (since 4.2)
 *   * Bits 57-60 zero
 *   * Bit  61    page is file-page or shared-anon (since 3.5)
 *   * Bit  62    page swapped
 *   * Bit  63    page present
 */

#define PFN_MASK 0x7FFFFFFFFFFFFF

static FILE* open_pagemap(pid_t target)
{
	char fname[64];

	/* PID 0 means "self" */
	if (!target) {
		snprintf(fname, sizeof(fname), "/proc/self/pagemap");
	} else {
		snprintf(fname, sizeof(fname), "/proc/%d/pagemap", (int) target);
	}

	return fopen(fname, "r");
}

static unsigned long get_desc(FILE* pmap, size_t pagesize, unsigned long virt_addr)
{
	unsigned long virt_page_num;
	off_t offset;
	uint64_t page_desc;

	virt_page_num  = virt_addr / pagesize;
	offset = virt_page_num * PAGEMAP_ENTRY_LEN;
	if (fseek(pmap, offset, SEEK_SET)) {
		perror("fseek");
		return 0;
	}

	if (fread(&page_desc, PAGEMAP_ENTRY_LEN, 1, pmap) != 1) {
		perror("fread");
		return 0;
	}

	return page_desc;
}

unsigned long get_phys_addr(pid_t target, unsigned long virt_addr)
{
	FILE *pmap;
	size_t pagesize = getpagesize();
	unsigned long phys_page_num;
	unsigned long offset_in_page;
	uint64_t page_desc;


	pmap = open_pagemap(target);
	if (!pmap) {
		perror("fopen");
		return 0;
	}

	offset_in_page = virt_addr % pagesize;
	page_desc = get_desc(pmap, pagesize, virt_addr);
	phys_page_num = page_desc & PFN_MASK;

	fclose(pmap);

	return (phys_page_num * pagesize) + offset_in_page;
}

size_t get_phys_addrs(
	pid_t target,
	unsigned long virt_addr_start,
	unsigned long virt_addr_end,
	unsigned long *phys_addr,
	size_t num_addr)
{
	FILE *pmap;
	size_t pagesize = getpagesize();
	unsigned long phys_page_num;
	unsigned long offset_in_page;
	uint64_t page_desc;
	unsigned long virt_addr;
	size_t count = 0;

	pmap = open_pagemap(target);
	if (!pmap) {
		perror("fopen");
		return 0;
	}

	virt_addr = virt_addr_start;
	while (virt_addr < virt_addr_end && count < num_addr) {
		offset_in_page = virt_addr % pagesize;
		page_desc = get_desc(pmap, pagesize, virt_addr);
		phys_page_num = page_desc & PFN_MASK;
		phys_addr[count++] = (phys_page_num * pagesize) + offset_in_page;
		virt_addr += pagesize;
	}


	fclose(pmap);
	return count;
}
