#ifndef __VFS_H__
#define __VFS_H__

#include <stdint.h>
#include <sys/types.h>

/* 文件类型 */
#define VFS_FILE    1
#define VFS_DIR     2
#define VFS_MOUNT   3
#define VFS_EXEC    4

/* 文件打开模式 */
#define VFS_O_RDONLY   0
#define VFS_O_WRONLY   1
#define VFS_O_RDWR     2
#define VFS_O_CREAT    4
#define VFS_O_TRUNC    8
#define VFS_O_APPEND   16

/* 文件状态 */
typedef struct {
    uint16_t type;      /* VFS_FILE 或 VFS_DIR */
    uint32_t size;
    char     name[64];
    uint32_t mtime;
} vfs_stat_t;

/* 目录项 */
typedef struct {
    char name[64];
    uint16_t type;
    uint32_t size;
    uint32_t mtime;
} vfs_dirent_t;

/* 目录流 */
typedef struct vfs_dir_s {
    char path[128];
    int index;
    vfs_dirent_t entries[64];
    int entry_count;
    void *dp;  /* POSIX DIR* */
} vfs_dir_t;

/* 文件流 */
typedef struct vfs_file_s {
    char path[128];
    int flags;
    uint32_t offset;
} vfs_file_t;

void vfs_register_partitions(void);
int  vfs_init(void);
int  vfs_format(void);
void vfs_info(uint32_t *total, uint32_t *used);

vfs_file_t *vfs_open(const char *path, int flags);
int  vfs_close(vfs_file_t *file);
int  vfs_read(vfs_file_t *file, void *buf, uint32_t size);
int  vfs_write(vfs_file_t *file, const void *buf, uint32_t size);
int  vfs_seek(vfs_file_t *file, int offset, int whence);
int  vfs_tell(vfs_file_t *file);
int  vfs_eof(vfs_file_t *file);
int  vfs_stat(const char *path, vfs_stat_t *stat);
int  vfs_exists(const char *path);

vfs_dir_t *vfs_opendir(const char *path);
vfs_dirent_t *vfs_readdir(vfs_dir_t *dir);
int  vfs_closedir(vfs_dir_t *dir);

int  vfs_mkdir(const char *path);
int  vfs_remove(const char *path);
int  vfs_rename(const char *from, const char *to);

int  vfs_getcwd(char *buf, int size);
int  vfs_chdir(const char *path);

#endif
