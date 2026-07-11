#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include "vfs.h"
#ifdef ELF_ECHO_DATA
#include "fonts/elf_echo.h"
#endif
#ifdef ELF_HELLO_DATA
#include "fonts/elf_hello.h"
#endif
#ifdef ELF_DATE_DATA
#include "fonts/elf_date.h"
#endif
#ifdef ELF_FREE_DATA
#include "fonts/elf_free.h"
#endif
#ifdef ELF_UNAME_DATA
#include "fonts/elf_uname.h"
#endif
#ifdef ELF_DF_DATA
#include "fonts/elf_df.h"
#endif
#ifdef ELF_EDIT_DATA
#include "fonts/elf_edit.h"
#endif

static const char *TAG = "VFS";
static char g_cwd[128] = "/";
static int g_mounted = 0;

/* SPIFFS根路径 */
#define SPIFFS_ROOT "/spiffs"

/* 将虚拟路径转换为SPIFFS实际路径 */
static void real_path(const char *vpath, char *real, int size)
{
    char resolved[256];

    if (vpath == NULL || vpath[0] == '\0') {
        snprintf(real, size, "%s%s", SPIFFS_ROOT, g_cwd);
        return;
    }

    if (vpath[0] == '/') {
        snprintf(resolved, sizeof(resolved), "%s", vpath);
    } else {
        snprintf(resolved, sizeof(resolved), "%s/%s", g_cwd, vpath);
    }

    /* 简化路径 */
    char *in = resolved;
    char out[256];
    int out_pos = 0;

    while (*in && out_pos < 255) {
        if (*in == '/') {
            out[out_pos++] = '/';
            in++;
            while (*in == '/') in++;
        } else if (*in == '.' && *(in + 1) == '.' && (*(in + 2) == '/' || *(in + 2) == '\0')) {
            /* ..: 后退一级 */
            if (out_pos > 1) {
                out_pos--;
                while (out_pos > 0 && out[out_pos - 1] != '/') out_pos--;
            }
            in += 2;
            if (*in == '/') in++;
        } else if (*in == '.' && (*(in + 1) == '/' || *(in + 1) == '\0')) {
            in++;
            if (*in == '/') in++;
        } else {
            while (*in && *in != '/' && out_pos < 255) {
                out[out_pos++] = *in++;
            }
        }
    }
    out[out_pos] = '\0';

    /* 去除末尾多余的/ */
    if (out_pos > 1 && out[out_pos - 1] == '/') {
        out[out_pos - 1] = '\0';
    }

    snprintf(real, size, "%s%s", SPIFFS_ROOT, out);
}

static void __attribute__((unused)) strip_prefix(const char *real, char *vpath, int size)
{
    const char *p = real + strlen(SPIFFS_ROOT);
    if (p[0] == '\0') {
        snprintf(vpath, size, "/");
    } else {
        snprintf(vpath, size, "%s", p);
    }
}

/* ---- 文件注册表 (替代 SPIFFS readdir) ---- */
#define MAX_REG 128
typedef struct {
    char name[64];
    uint16_t type;
} reg_entry_t;
static reg_entry_t s_registry[MAX_REG];
static int s_reg_count = 0;

static int reg_find(const char *name)
{
    for (int i = 0; i < s_reg_count; i++)
        if (strcmp(s_registry[i].name, name) == 0) return i;
    return -1;
}

static void reg_add(const char *name, uint16_t type)
{
    if (reg_find(name) >= 0) return;
    if (s_reg_count >= MAX_REG) return;
    strncpy(s_registry[s_reg_count].name, name, sizeof(s_registry[s_reg_count].name) - 1);
    s_registry[s_reg_count].type = type;
    s_reg_count++;
    /* 同时添加父目录 */
    char buf[64];
    strncpy(buf, name, sizeof(buf) - 1);
    char *p = strrchr(buf, '/');
    if (p && p != buf) {
        *p = '\0';
        reg_add(buf, VFS_DIR);
    }
}

static void __attribute__((unused)) reg_remove(const char *name)
{
    int i = reg_find(name);
    if (i < 0) return;
    s_reg_count--;
    s_registry[i] = s_registry[s_reg_count];
}

/* 安装内嵌 ELF 文件到 /bin/ */
static void install_elf(const char *name, const uint8_t *data, uint32_t size)
{
    char path[64];
    snprintf(path, sizeof(path), SPIFFS_ROOT "/bin/%s", name);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
        ESP_LOGI(TAG, "安装命令: /bin/%s (%lu bytes)", name, (unsigned long)size);
    }
    char vpath[64];
    snprintf(vpath, sizeof(vpath), "/bin/%s", name);
    reg_add(vpath, VFS_EXEC);
}

/* ---- 绕过QEMU下flash/分区访问问题 ---- */

/*
 * QEMU的ESP32-S3模拟在PSRAM remap后MMU页表损坏,
 * 导致spi_flash_mmap(调用esp_mmu_map)返回ESP_ERR_NO_MEM (0x101),
 * 以及rom_spiflash_api_funcs->chip_check崩溃(函数表指针无效).
 *
 * 解决方案:
 *   1. --wrap=spi_flash_mmap: 用esp_flash_read替代MMU映射读取flash数据
 *   2. --wrap=spi_flash_munmap: 无操作(数据已在缓冲区)
 *   3. --wrap=esp_partition_find_first: 可选优化, 直接返回硬编码分区表
 *      (即使load_partitions通过修复的spi_flash_mmap可以工作)
 */

/* ---- spi_flash_mmap 包装 ---- */

#include "spi_flash_mmap.h"
#include "esp_flash.h"

/*
 * 静态缓冲区: 模拟一个MMU页面大小的映射.
 * ESP32-S3的CONFIG_MMU_PAGE_SIZE=0x10000(64KB).
 * 这样即使caller在映射地址上加偏移(如load_partitions从0x0加0x8000)也能访问.
 */
#define MMAP_BUF_SIZE CONFIG_MMU_PAGE_SIZE
static uint8_t s_mmap_buf[MMAP_BUF_SIZE] __attribute__((aligned(16)));
static int s_mmap_buf_used;

/* __real_spi_flash_mmap 由链接器生成 */
esp_err_t __real_spi_flash_mmap(size_t src_addr, size_t size,
    spi_flash_mmap_memory_t memory, const void **out_ptr,
    spi_flash_mmap_handle_t *out_handle);

esp_err_t __wrap_spi_flash_mmap(size_t src_addr, size_t size,
    spi_flash_mmap_memory_t memory, const void **out_ptr,
    spi_flash_mmap_handle_t *out_handle)
{
    if (size > MMAP_BUF_SIZE) {
        return __real_spi_flash_mmap(src_addr, size, memory, out_ptr, out_handle);
    }
    if (s_mmap_buf_used) {
        return __real_spi_flash_mmap(src_addr, size, memory, out_ptr, out_handle);
    }

    extern esp_flash_t *esp_flash_default_chip;
    if (esp_flash_default_chip == NULL) {
        return ESP_ERR_FLASH_NOT_INITIALISED;
    }

    s_mmap_buf_used = 1;
    esp_err_t ret = esp_flash_read(esp_flash_default_chip, s_mmap_buf, src_addr, MMAP_BUF_SIZE);
    if (ret != ESP_OK) {
        s_mmap_buf_used = 0;
        return ret;
    }

    *out_ptr = s_mmap_buf;
    *out_handle = (spi_flash_mmap_handle_t)(1);
    return ESP_OK;
}

void __wrap_spi_flash_munmap(spi_flash_mmap_handle_t handle)
{
    (void)handle;
    s_mmap_buf_used = 0;
}

/* ---- esp_partition_find_first 包装 (可选优化) ---- */

/* 预分配的静态分区对象 (避免heap分配, 仅用于return) */
static esp_partition_t s_qemu_partitions[] = {
    { NULL, ESP_PARTITION_TYPE_DATA, 0x02, 0x9000,   0x4000,  4096, "nvs",      false, false },
    { NULL, ESP_PARTITION_TYPE_DATA, 0x00, 0xd000,   0x2000,  4096, "otadata",  false, false },
    { NULL, ESP_PARTITION_TYPE_DATA, 0x01, 0xf000,   0x1000,  4096, "phy_init", false, false },
    { NULL, ESP_PARTITION_TYPE_APP,  0x00, 0x10000,  0x400000,4096, "app",      false, false },
    { NULL, ESP_PARTITION_TYPE_DATA, 0x82, 0x410000, 0xBF0000,4096, "littlefs", false, false },
};

/* __real_esp_partition_find_first 由链接器在--wrap时自动生成 */
const esp_partition_t *__real_esp_partition_find_first(
    esp_partition_type_t type, esp_partition_subtype_t subtype, const char *label);

/* 包装函数: 先匹配硬编码表, 失败返回NULL (不调用系统路径以避免load_partitions) */
const esp_partition_t *__wrap_esp_partition_find_first(
    esp_partition_type_t type, esp_partition_subtype_t subtype, const char *label)
{
    for (int i = 0; i < sizeof(s_qemu_partitions)/sizeof(s_qemu_partitions[0]); i++) {
        esp_partition_t *p = &s_qemu_partitions[i];
        bool type_match  = (type == ESP_PARTITION_TYPE_ANY || p->type == type);
        bool sub_match   = (subtype == ESP_PARTITION_SUBTYPE_ANY || p->subtype == subtype);
        bool label_match = (label == NULL || strcmp(p->label, label) == 0);
        if (type_match && sub_match && label_match) {
            extern esp_flash_t *esp_flash_default_chip;
            p->flash_chip = esp_flash_default_chip;
            return p;
        }
    }
    return NULL;
}

void vfs_register_partitions(void)
{
    /* 检查已有分区 (会经过我们的wrap) */
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (p != NULL && p >= s_qemu_partitions && 
        p < s_qemu_partitions + sizeof(s_qemu_partitions)/sizeof(s_qemu_partitions[0])) {
        ESP_LOGI(TAG, "分区表已由wrap提供");
        return;
    }
    if (p != NULL) {
        ESP_LOGI(TAG, "分区表已存在");
        return;
    }

    ESP_LOGI(TAG, "QEMU分区wrap已就绪");
    (void)p;
}

/* ---- 公共API ---- */

int vfs_init(void)
{
    ESP_LOGI(TAG, "mount spiffs");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_ROOT,
        .partition_label = "littlefs",
        .max_files = 16,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "SPIFFS挂载失败 (可能需要格式化)");
        } else {
            ESP_LOGE(TAG, "SPIFFS初始化失败: %s", esp_err_to_name(ret));
        }
        return -1;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("littlefs", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "mount ok (%dK/%dK)", (int)(total / 1024), (int)(used / 1024));
    }

    g_mounted = 1;

    /* 初始化文件注册表 */
    reg_add("/", VFS_DIR);
    reg_add("/bin", VFS_DIR);
    reg_add("/home", VFS_DIR);
    reg_add("/tmp", VFS_DIR);
    reg_add("/etc", VFS_DIR);
    reg_add("/dev", VFS_DIR);
    reg_add("/mnt", VFS_DIR);

    /* 确保 /bin 存在 (SPIFFS镜像已含ELF文件, 但格式化后需要重建) */
    mkdir(SPIFFS_ROOT "/bin", 0777);
    mkdir(SPIFFS_ROOT "/home", 0777);
    mkdir(SPIFFS_ROOT "/tmp", 0777);
    mkdir(SPIFFS_ROOT "/etc", 0777);
    mkdir(SPIFFS_ROOT "/dev", 0777);
    mkdir(SPIFFS_ROOT "/mnt", 0777);

    /* 创建欢迎文件 */
    FILE *f = fopen(SPIFFS_ROOT "/home/welcome.txt", "w");
    if (f) {
        fprintf(f, "ESP-DOS Version 1.0\n");
        fprintf(f, "(C) Copyright Jaycomp 2026\n");
        fclose(f);
        reg_add("/home/welcome.txt", VFS_FILE);
    }

    /* 安装 ELF 命令 */
    mkdir(SPIFFS_ROOT "/bin", 0777);
#ifdef ELF_ECHO_DATA
    install_elf("echo", elf_echo_data, ELF_ECHO_SIZE);
#endif
#ifdef ELF_HELLO_DATA
    install_elf("hello", elf_hello_data, ELF_HELLO_SIZE);
#endif
#ifdef ELF_DATE_DATA
    install_elf("date", elf_date_data, ELF_DATE_SIZE);
#endif
#ifdef ELF_FREE_DATA
    install_elf("mem", elf_free_data, ELF_FREE_SIZE);
#endif
#ifdef ELF_UNAME_DATA
    install_elf("ver", elf_uname_data, ELF_UNAME_SIZE);
#endif
#ifdef ELF_DF_DATA
    install_elf("chkdsk", elf_df_data, ELF_DF_SIZE);
#endif
#ifdef ELF_EDIT_DATA
    install_elf("edit", elf_edit_data, ELF_EDIT_SIZE);
#endif

    ESP_LOGI(TAG, "SPIFFS ready");
    return 0;
}

int vfs_format(void)
{
    ESP_LOGI(TAG, "格式化SPIFFS...");
    esp_err_t ret = esp_spiffs_format("littlefs");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "格式化失败: %s", esp_err_to_name(ret));
        return -1;
    }
    ESP_LOGI(TAG, "格式化完成");
    return 0;
}

void vfs_info(uint32_t *total, uint32_t *used)
{
    size_t t, u;
    if (esp_spiffs_info("littlefs", &t, &u) == ESP_OK) {
        *total = (uint32_t)t;
        *used = (uint32_t)u;
    } else {
        *total = 0;
        *used = 0;
    }
}

vfs_file_t *vfs_open(const char *path, int flags)
{
    if (!g_mounted) return NULL;

    char real[256];
    real_path(path, real, sizeof(real));

    struct stat st;
    if (stat(real, &st) == 0 && S_ISDIR(st.st_mode)) {
        return NULL;
    }

    vfs_file_t *file = calloc(1, sizeof(vfs_file_t));
    if (!file) return NULL;

    strncpy(file->path, path, sizeof(file->path) - 1);
    file->flags = flags;

    /* 写模式: 创建或截断文件 */
    if (flags & (VFS_O_WRONLY | VFS_O_CREAT)) {
        const char *mode = (flags & VFS_O_APPEND) ? "ab" : "wb";
        FILE *f = fopen(real, mode);
        if (f) {
            fclose(f);
            char norm[256];
            if (path[0] == '/') {
                strncpy(norm, path, sizeof(norm) - 1);
            } else if (path[0] == '.' && path[1] == '\0') {
                snprintf(norm, sizeof(norm), "%s", g_cwd);
            } else {
                snprintf(norm, sizeof(norm), "%s/%s", g_cwd, path);
            }
            int nlen = strlen(norm);
            while (nlen > 1 && norm[nlen - 1] == '/') norm[--nlen] = '\0';
            reg_add(norm, VFS_FILE);
        } else {
            free(file);
            return NULL;
        }
    }

    return file;
}

int vfs_close(vfs_file_t *file)
{
    if (!file) return -1;
    free(file);
    return 0;
}

int vfs_read(vfs_file_t *file, void *buf, uint32_t size)
{
    if (!file || !g_mounted) return -1;
    char real[256];
    real_path(file->path, real, sizeof(real));

    FILE *f = fopen(real, "rb");
    if (!f) return -1;

    fseek(f, file->offset, SEEK_SET);
    int read = fread(buf, 1, size, f);
    file->offset += read;
    fclose(f);
    return read;
}

int vfs_write(vfs_file_t *file, const void *buf, uint32_t size)
{
    if (!file || !g_mounted) return -1;
    char real[256];
    real_path(file->path, real, sizeof(real));

    /* 使用追加模式以支持多次写入 */
    FILE *f = fopen(real, "ab");
    if (!f) return -1;

    int written = fwrite(buf, 1, size, f);
    file->offset += written;
    fclose(f);
    return written;
}

int vfs_stat(const char *path, vfs_stat_t *statbuf)
{
    if (!g_mounted) return -1;
    char real[256];
    real_path(path, real, sizeof(real));

    struct stat st;
    if (stat(real, &st) != 0) return -1;

    memset(statbuf, 0, sizeof(vfs_stat_t));
    if (S_ISDIR(st.st_mode)) {
        statbuf->type = VFS_DIR;
    } else {
        statbuf->type = VFS_FILE;
        /* 检测 ELF 魔数标记可执行文件 */
        FILE *fp = fopen(real, "rb");
        if (fp) {
            unsigned char magic[4];
            if (fread(magic, 1, 4, fp) == 4 && magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
                statbuf->type = VFS_EXEC;
            }
            fclose(fp);
        }
    }
    statbuf->size = (uint32_t)st.st_size;
    statbuf->mtime = (uint32_t)st.st_mtime;

    const char *name = strrchr(path, '/');
    if (name) {
        snprintf(statbuf->name, sizeof(statbuf->name), "%s", name + 1);
    } else {
        snprintf(statbuf->name, sizeof(statbuf->name), "%s", path);
    }

    return 0;
}

int vfs_exists(const char *path)
{
    vfs_stat_t st;
    return (vfs_stat(path, &st) == 0);
}

/* 目录流 (全局单例) */
static vfs_dir_t s_dir;

vfs_dir_t *vfs_opendir(const char *path)
{
    if (!g_mounted) return NULL;

    /* 规范化路径 */
    char norm[256];
    if (path[0] == '/') {
        strncpy(norm, path, sizeof(norm) - 1);
    } else if (path[0] == '.' && path[1] == '\0') {
        snprintf(norm, sizeof(norm), "%s", g_cwd);
    } else if (path[0] == '.' && path[1] == '.' && path[2] == '\0') {
        strncpy(norm, g_cwd, sizeof(norm) - 1);
        char *p = strrchr(norm, '/');
        if (p && p != norm) *p = '\0';
        else norm[1] = '\0';
    } else {
        snprintf(norm, sizeof(norm), "%s/%s", g_cwd, path);
    }
    int nlen = strlen(norm);
    while (nlen > 1 && norm[nlen - 1] == '/') { norm[--nlen] = '\0'; }

    memset(&s_dir, 0, sizeof(s_dir));
    strncpy(s_dir.path, norm, sizeof(s_dir.path) - 1);

    /* 遍历注册表, 返回 norm 的直接子项 */
    for (int i = 0; i < s_reg_count && s_dir.entry_count < 64; i++) {
        const char *full = s_registry[i].name;
        if (strcmp(full, norm) == 0) continue;            /* 跳过自身 */
        if (strncmp(full, norm, nlen) != 0) continue;      /* 前缀不匹配 */
        if (full[nlen] == '\0') continue;                  /* 完全匹配(已被跳过) */
        if (nlen > 1 && full[nlen] != '/') continue;       /* 非根目录需要 / 分隔 */
        const char *rest = full + nlen;
        if (*rest == '/') rest++;
        if (strchr(rest, '/')) continue;                    /* 更深层子项, 跳过 */
        int idx = s_dir.entry_count;
        strncpy(s_dir.entries[idx].name, rest,
                sizeof(s_dir.entries[idx].name) - 1);
        s_dir.entries[idx].name[sizeof(s_dir.entries[idx].name) - 1] = '\0';
        s_dir.entries[idx].type = s_registry[i].type;
        s_dir.entries[idx].size = 0;
        s_dir.entries[idx].mtime = 0;
        /* 获取文件实际大小和时间 */
        if (s_registry[i].type == VFS_FILE || s_registry[i].type == VFS_EXEC || s_registry[i].type == VFS_DIR) {
            char fpath[128];
            real_path(full, fpath, sizeof(fpath));
            struct stat st;
            if (stat(fpath, &st) == 0) {
                s_dir.entries[idx].size = (uint32_t)st.st_size;
                s_dir.entries[idx].mtime = (uint32_t)st.st_mtime;
            }
        }
        s_dir.entry_count++;
    }

    s_dir.index = 0;
    return &s_dir;
}

vfs_dirent_t *vfs_readdir(vfs_dir_t *dir)
{
    if (!dir) return NULL;
    if (dir->index >= dir->entry_count) return NULL;
    return &dir->entries[dir->index++];
}

int vfs_closedir(vfs_dir_t *dir)
{
    if (!dir) return -1;
    memset(dir, 0, sizeof(*dir));
    return 0;
}

int vfs_mkdir(const char *path)
{
    if (!g_mounted) return -1;
    char real[256];
    real_path(path, real, sizeof(real));

    if (mkdir(real, 0777) != 0) {
        if (errno == EEXIST) return 0;
        return -1;
    }
    return 0;
}

int vfs_remove(const char *path)
{
    if (!g_mounted) return -1;
    char real[256];
    real_path(path, real, sizeof(real));

    struct stat st;
    if (stat(real, &st) != 0) return -1;

    if (S_ISDIR(st.st_mode)) {
        return rmdir(real) == 0 ? 0 : -1;
    } else {
        return unlink(real) == 0 ? 0 : -1;
    }
}

int vfs_rename(const char *from, const char *to)
{
    if (!g_mounted) return -1;
    char real_from[256], real_to[256];
    real_path(from, real_from, sizeof(real_from));
    real_path(to, real_to, sizeof(real_to));
    return rename(real_from, real_to) == 0 ? 0 : -1;
}

int vfs_getcwd(char *buf, int size)
{
    snprintf(buf, size, "%s", g_cwd);
    return 0;
}

int vfs_chdir(const char *path)
{
    if (!g_mounted) return -1;

    /* 注册表中查找目录 */
    char norm[256];
    if (path[0] == '/') {
        strncpy(norm, path, sizeof(norm) - 1);
    } else {
        int clen = strlen(g_cwd);
        if (g_cwd[clen - 1] == '/')
            snprintf(norm, sizeof(norm), "%s%s", g_cwd, path);
        else
            snprintf(norm, sizeof(norm), "%s/%s", g_cwd, path);
    }
    /* 去掉尾部 / */
    int nlen = strlen(norm);
    while (nlen > 1 && norm[nlen - 1] == '/') norm[--nlen] = '\0';

    if (reg_find(norm) < 0 || s_registry[reg_find(norm)].type != VFS_DIR)
        return -1;

    strncpy(g_cwd, norm, sizeof(g_cwd) - 1);
    return 0;
}

int vfs_seek(vfs_file_t *file, int offset, int whence)
{
    if (!file) return -1;
    switch (whence) {
        case SEEK_SET: file->offset = offset; break;
        case SEEK_CUR: file->offset += offset; break;
        case SEEK_END: {
            vfs_stat_t st;
            if (vfs_stat(file->path, &st) == 0) {
                file->offset = st.size + offset;
            }
            break;
        }
        default: return -1;
    }
    return 0;
}

int vfs_tell(vfs_file_t *file)
{
    if (!file) return -1;
    return file->offset;
}

int vfs_eof(vfs_file_t *file)
{
    if (!file) return 1;
    vfs_stat_t st;
    if (vfs_stat(file->path, &st) != 0) return 1;
    return file->offset >= st.size;
}
