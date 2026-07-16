#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp32s3/rom/cache.h"
#include "hal/mmu_ll.h"
#include "soc/ext_mem_defs.h"
#include "elf.h"
#include "vfs.h"

static const char *TAG = "ELF";

static int elf_validate(Elf32_Ehdr *ehdr)
{
    if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3) {
        ESP_LOGE(TAG, "bad ELF magic"); return -1;
    }
    if (ehdr->e_machine != EM_XTENSA) {
        ESP_LOGE(TAG, "not Xtensa (machine=%d)", ehdr->e_machine); return -1;
    }
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        ESP_LOGE(TAG, "not executable (type=%d)", ehdr->e_type); return -1;
    }
    return 0;
}

int elf_load(const char *path, elf_load_result_t *out)
{
    memset(out, 0, sizeof(*out));

    vfs_file_t *f = vfs_open(path, VFS_O_RDONLY);
    if (!f) { ESP_LOGE(TAG, "cannot open: %s", path); return -1; }

    Elf32_Ehdr ehdr;
    if (vfs_read(f, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) { vfs_close(f); return -1; }
    if (elf_validate(&ehdr) != 0) { vfs_close(f); return -1; }

    int phnum = ehdr.e_phnum;
    if (phnum <= 0 || phnum > 16) { vfs_close(f); return -1; }

    Elf32_Phdr *phdr = malloc(sizeof(Elf32_Phdr) * phnum);
    if (!phdr) { vfs_close(f); return -1; }
    vfs_seek(f, ehdr.e_phoff, SEEK_SET);
    if (vfs_read(f, phdr, sizeof(Elf32_Phdr) * phnum) != (int)(sizeof(Elf32_Phdr) * phnum)) {
        free(phdr); vfs_close(f); return -1;
    }

    uint32_t min_vaddr = 0xFFFFFFFF, max_vaddr = 0;
    for (int i = 0; i < phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint32_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
        if (phdr[i].p_vaddr < min_vaddr) min_vaddr = phdr[i].p_vaddr;
        if (end > max_vaddr) max_vaddr = end;
    }
    if (min_vaddr == 0xFFFFFFFF) { free(phdr); vfs_close(f); return -1; }
    uint32_t total_size = max_vaddr - min_vaddr;

    void *base = heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM);
    if (!base) {
        ESP_LOGE(TAG, "PSRAM alloc failed (%lu bytes)", (unsigned long)total_size);
        free(phdr); vfs_close(f); return -1;
    }
    memset(base, 0, total_size);

    for (int i = 0; i < phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        void *dest = (uint8_t *)base + (phdr[i].p_vaddr - min_vaddr);
        if (phdr[i].p_filesz > 0) {
            vfs_seek(f, phdr[i].p_offset, SEEK_SET);
            if (vfs_read(f, dest, phdr[i].p_filesz) != (int)phdr[i].p_filesz) {
                free(base); free(phdr); vfs_close(f); return -1;
            }
        }
    }
    free(phdr);
    vfs_close(f);

    int32_t delta = (uint32_t)base - min_vaddr;
    if (delta != 0) {
        uint32_t *p = (uint32_t *)base, *end = (uint32_t *)((uint8_t *)base + total_size);
        while (p < end) { if ((*p & 0xFFF00000) == 0x3F000000) *p += delta; p++; }
        ESP_LOGI(TAG, "reloc delta=0x%x", (unsigned)delta);
    }

    Cache_WriteBack_Addr((uint32_t)base, total_size);

    void *exec = base;
    uint32_t page_size = 0x10000;
    uint32_t off = (uint32_t)base & (page_size - 1);
    uint32_t map_size = ((off + total_size + page_size - 1) / page_size) * page_size;
    uint32_t pages = map_size / page_size;
    uint32_t entry_id = mmu_ll_get_entry_id(0, (uint32_t)base);
    uint32_t entry_val = mmu_ll_read_entry(0, entry_id);

    if (!(entry_val & MMU_INVALID)) {
        uint32_t phys_page = entry_val & 0x3FFFF;
        uint32_t phys_base = phys_page << 16;
        uint32_t ibase = SOC_MMU_IBUS_VADDR_BASE;
        uint32_t ientry = mmu_ll_get_entry_id(0, ibase);
        int found = -1;
        for (int i = 0; i < 512 - (int)pages; i++) {
            int ok = 1;
            for (uint32_t p = 0; p < pages; p++) {
                if (!mmu_ll_get_entry_is_invalid(0, ientry + i + p)) { ok = 0; break; }
            }
            if (ok) { found = i; break; }
        }
        if (found >= 0) {
            for (uint32_t p = 0; p < pages; p++) {
                uint32_t pa = phys_base + p * page_size;
                uint32_t fval = mmu_ll_format_paddr(0, pa);
                mmu_ll_write_entry(0, ientry + found + p, fval, MMU_TARGET_PSRAM0);
            }
            exec = (void *)(ibase + (uint32_t)found * page_size + off);
            Cache_Invalidate_Addr((uint32_t)exec, map_size);
            ESP_LOGI(TAG, "mapped: data=%p -> exec=%p (%luK)", base, exec, (unsigned long)(map_size / 1024));
        } else {
            ESP_LOGW(TAG, "no free I$ slot, running from PSRAM data addr");
        }
    } else {
        ESP_LOGW(TAG, "PSRAM MMU invalid, running from data addr");
    }

    out->base_addr = base;
    out->entry_point = (uint8_t *)exec + (ehdr.e_entry - min_vaddr);
    out->size = total_size;
    out->valid = 1;

    ESP_LOGI(TAG, "loaded '%s': entry=%p size=%lu", path, out->entry_point, (unsigned long)total_size);
    return 0;
}

void elf_free(elf_load_result_t *elf)
{
    if (elf && elf->base_addr) {
        free(elf->base_addr);
        elf->base_addr = NULL;
        elf->valid = 0;
    }
}
