/* File system interface used by the shell (implemented by kernel/fs/p5fs.c).
 * Paths may be absolute or relative to the current directory. */
#ifndef P5_FS_H
#define P5_FS_H
#include <types.h>

#define FS_PATH_MAX 128

struct fs_entry {
    char name[28];
    u32 ino;
    u16 type;               /* P5FS_T_FILE / P5FS_T_DIR */
    u32 size;
    u32 mtime;
};

struct fs_info {
    char label[17];
    char device[4];
    u32 part_start;         /* LBA of the file system on the device */
    u32 total_blocks, free_blocks, data_blocks;
    u32 inode_count, inodes_used;
};

typedef bool (*fs_list_cb)(const struct fs_entry *e, void *ctx);   /* false = stop */

int fs_mount_auto(void);
bool fs_is_mounted(void);
const char *fs_cwd(void);               /* NULL when nothing is mounted */
int fs_chdir(const char *path);
int fs_stat(const char *path, struct fs_entry *e);
int fs_list(const char *path, fs_list_cb cb, void *ctx);
int fs_read_file(const char *path, void **data, u32 *len);     /* kfree() data */
int fs_write_file(const char *path, const void *data, u32 len, bool append);
int fs_mkdir(const char *path);
int fs_remove(const char *path);
int fs_info(struct fs_info *out);
int fs_check(bool verbose);             /* >=0: number of problems found */
#endif
