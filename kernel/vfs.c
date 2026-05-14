#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include "esp_spiffs.h"
#include "esp_log.h"
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

static void strip_prefix(const char *real, char *vpath, int size)
{
    const char *p = real + strlen(SPIFFS_ROOT);
    if (p[0] == '\0') {
        snprintf(vpath, size, "/");
    } else {
        snprintf(vpath, size, "%s", p);
    }
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
        ESP_LOGI(TAG, "安装命令: /bin/%s (%d bytes)", name, size);
    }
}

/* ---- 公共API ---- */

int vfs_init(void)
{
    ESP_LOGI(TAG, "初始化SPIFFS文件系统...");

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
        ESP_LOGI(TAG, "SPIFFS: 总空间 %d KB, 已用 %d KB", (int)(total / 1024), (int)(used / 1024));
    }

    g_mounted = 1;

    /* 确保根目录存在 */
    mkdir(SPIFFS_ROOT "/home", 0777);
    mkdir(SPIFFS_ROOT "/tmp", 0777);
    mkdir(SPIFFS_ROOT "/etc", 0777);
    mkdir(SPIFFS_ROOT "/bin", 0777);
    mkdir(SPIFFS_ROOT "/dev", 0777);
    mkdir(SPIFFS_ROOT "/mnt", 0777);

    /* 创建欢迎文件 */
    FILE *f = fopen(SPIFFS_ROOT "/home/welcome.txt", "w");
    if (f) {
        fprintf(f, "欢迎使用 OpenCrab-DOS!\n");
        fprintf(f, "输入 HELP 查看可用命令。\n");
        fclose(f);
    }

    /* 确保 /bin 存在 (SPIFFS镜像已含ELF文件, 但格式化后需要重建) */
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

    ESP_LOGI(TAG, "SPIFFS 初始化完成");
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
    statbuf->type = S_ISDIR(st.st_mode) ? VFS_DIR : VFS_FILE;
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

vfs_dir_t *vfs_opendir(const char *path)
{
    if (!g_mounted) return NULL;

    char real[256];
    real_path(path, real, sizeof(real));

    DIR *dir = opendir(real);
    if (!dir) return NULL;

    vfs_dir_t *vd = calloc(1, sizeof(vfs_dir_t));
    if (!vd) { closedir(dir); return NULL; }

    real_path(path, vd->path, sizeof(vd->path));

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < 64) {
        strncpy(vd->entries[count].name, entry->d_name, sizeof(vd->entries[count].name) - 1);
        vd->entries[count].name[sizeof(vd->entries[count].name) - 1] = '\0';

        char full[512];
        snprintf(full, sizeof(full), "%s/%s", real, entry->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            vd->entries[count].type = S_ISDIR(st.st_mode) ? VFS_DIR : VFS_FILE;
            vd->entries[count].size = (uint32_t)st.st_size;
        } else {
            vd->entries[count].type = VFS_FILE;
            vd->entries[count].size = 0;
        }
        count++;
    }
    closedir(dir);

    vd->entry_count = count;
    vd->index = 0;
    return vd;
}

vfs_dirent_t *vfs_readdir(vfs_dir_t *dir)
{
    if (!dir || dir->index >= dir->entry_count) return NULL;
    return &dir->entries[dir->index++];
}

int vfs_closedir(vfs_dir_t *dir)
{
    if (!dir) return -1;
    free(dir);
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
    char real[256];
    real_path(path, real, sizeof(real));

    struct stat st;
    if (stat(real, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return -1;
    }

    char resolved[256];
    real_path(path, resolved, sizeof(resolved));
    strip_prefix(resolved, g_cwd, sizeof(g_cwd));

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
