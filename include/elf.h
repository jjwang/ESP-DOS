#ifndef __ELF_H__
#define __ELF_H__

#include <stdint.h>

#define EI_NIDENT 16
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define EM_XTENSA 94
#define ET_EXEC 2
#define ET_DYN 3
#define PT_LOAD 1

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

/* ELF加载结果 */
typedef struct {
    void *base_addr;     /* PSRAM中加载基址 */
    void *entry_point;   /* 入口函数指针 */
    uint32_t size;       /* 总占用内存 */
    int valid;
} elf_load_result_t;

int elf_load(const char *path, elf_load_result_t *out);
void elf_free(elf_load_result_t *elf);

#endif
