#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "hal/mmu_ll.h"
#include "hal/mmu_types.h"
#include "esp32s3/rom/cache.h"
#include "soc/ext_mem_defs.h"
#include "elf.h"
#include "vfs.h"

static const char *TAG = "ELF";

/* 对齐到cache line (32字节) */
#define CACHE_LINE 32
#define ROUND_UP(n, a) (((n) + (a) - 1) & ~((a) - 1))

static int elf_validate(Elf32_Ehdr *ehdr)
{
    if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3) {
        ESP_LOGE(TAG, "无效 ELF 魔数");
        return -1;
    }
    if (ehdr->e_machine != EM_XTENSA) {
        ESP_LOGE(TAG, "非 Xtensa 架构 (machine=%d)", ehdr->e_machine);
        return -1;
    }
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        ESP_LOGE(TAG, "非可执行 ELF (type=%d)", ehdr->e_type);
        return -1;
    }
    return 0;
}

int elf_load(const char *path, elf_load_result_t *out)
{
    memset(out, 0, sizeof(*out));

    vfs_file_t *f = vfs_open(path, VFS_O_RDONLY);
    if (!f) {
        ESP_LOGE(TAG, "无法打开: %s", path);
        return -1;
    }

    Elf32_Ehdr ehdr;
    if (vfs_read(f, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        vfs_close(f);
        return -1;
    }

    if (elf_validate(&ehdr) != 0) {
        vfs_close(f);
        return -1;
    }

    int phnum = ehdr.e_phnum;
    if (phnum <= 0 || phnum > 16) {
        vfs_close(f);
        return -1;
    }

    Elf32_Phdr *phdr = malloc(sizeof(Elf32_Phdr) * phnum);
    if (!phdr) { vfs_close(f); return -1; }

    vfs_seek(f, ehdr.e_phoff, SEEK_SET);
    if (vfs_read(f, phdr, sizeof(Elf32_Phdr) * phnum) != (int)(sizeof(Elf32_Phdr) * phnum)) {
        free(phdr); vfs_close(f); return -1;
    }

    uint32_t min_vaddr = 0xFFFFFFFF;
    uint32_t max_vaddr = 0;
    for (int i = 0; i < phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint32_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
        if (phdr[i].p_vaddr < min_vaddr) min_vaddr = phdr[i].p_vaddr;
        if (end > max_vaddr) max_vaddr = end;
    }
    if (min_vaddr == 0xFFFFFFFF) {
        free(phdr); vfs_close(f); return -1;
    }
    uint32_t total_size = max_vaddr - min_vaddr;

    /* 分配PSRAM (SPIRAM) */
    void *base = heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM);
    if (!base) {
        ESP_LOGE(TAG, "内存不足, 需要 %lu bytes", (unsigned long)total_size);
        free(phdr); vfs_close(f); return -1;
    }
    memset(base, 0, total_size);

    /* 加载ELF段到PSRAM */
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

    /* 重定位: 调整绝对地址 (min_vaddr=0x3f000000为链接基址) */
    int32_t delta = (uint32_t)base - min_vaddr;
    if (delta != 0) {
        /* 扫描.text中的Xtensa literal池, 调整R_XTENSA_32 (绝对地址) */
        uint32_t *p = (uint32_t *)base;
        uint32_t *end = (uint32_t *)((uint8_t *)base + total_size);
        while (p < end) {
            uint32_t val = *p;
            /* 检查是否是0x3fxxxxxx范围内的地址 (链接地址空间) */
            if ((val & 0xFFF00000) == 0x3F000000) {
                *p = val + delta;
            }
            p++;
        }
        ESP_LOGI(TAG, "重定位: delta=0x%x", (unsigned)delta);
    }

    /* 写回DCache, 确保数据到达PSRAM */
    Cache_WriteBack_Addr((uint32_t)base, total_size);

    /* 将PSRAM物理页映射到ICache (使用低层级MMU LL API, 兼容IDF 5.x) */
    void *exec_addr = base;
    do {
        uintptr_t vaddr = (uintptr_t)base;
        /* 确保地址在外部内存范围内 */
        if ((vaddr & SOC_MMU_LINEAR_ADDR_MASK) != vaddr) break;

        uint32_t page_size = 0x10000;
        uint32_t offset_in_page = vaddr & (page_size - 1);
        size_t map_size = ROUND_UP(offset_in_page + total_size, page_size);
        uint32_t pages = map_size / page_size;

        /* 读取数据地址对应的MMU表项, 得到物理页号 */
        uint32_t data_entry_id = mmu_ll_get_entry_id(0, vaddr);
        uint32_t mmu_val = mmu_ll_read_entry(0, data_entry_id);
        if (mmu_val & MMU_INVALID) break;

        /* 找一段空闲的指令虚地址 */
        uint32_t ibus_vaddr = SOC_MMU_IBUS_VADDR_BASE;
        uint32_t ibus_entry = mmu_ll_get_entry_id(0, ibus_vaddr);
        int found = 0;
        for (int i = 0; i < 512 - pages; i++) {
            int ok = 1;
            for (uint32_t p = 0; p < pages; p++) {
                if (!mmu_ll_get_entry_is_invalid(0, ibus_entry + i + p)) { ok = 0; break; }
            }
            if (ok) { found = i; break; }
        }
        if (!found) { ESP_LOGW(TAG, "无空闲ICache MMU槽"); break; }

        /* 逐页设置MMU表项 */
        uint32_t paddr_base = (mmu_val & (~0xFFFF)) << 16;
        for (uint32_t p = 0; p < pages; p++) {
            uint32_t phys = paddr_base + p * page_size;
            uint32_t formatted = mmu_ll_format_paddr(0, phys);
            mmu_ll_write_entry(0, ibus_entry + found + p, formatted, MMU_TARGET_PSRAM0);
        }

        exec_addr = (void *)(ibus_vaddr + found * page_size + offset_in_page);
        Cache_Invalidate_Addr((uint32_t)exec_addr, map_size);
        ESP_LOGI(TAG, "映射到ICache: data=%p exec=%p size=%luK", base, exec_addr,
                 (unsigned long)(map_size / 1024));
    } while (0);

    out->base_addr = base;
    out->entry_point = (uint8_t *)exec_addr + (ehdr.e_entry - min_vaddr);
    out->size = total_size;
    out->valid = 1;

    ESP_LOGI(TAG, "加载 '%s' 完成: entry=%p size=%lu", path, out->entry_point, (unsigned long)total_size);
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
