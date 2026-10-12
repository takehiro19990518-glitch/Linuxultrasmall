/* P5FS on-disk format, version 1.  Mirrored by tools/p5fs.py (see the
 * description there).  Block size = 512 bytes = one sector. */
#ifndef P5_P5FS_H
#define P5_P5FS_H
#include <types.h>

#define P5FS_MAGIC              0x53463550u     /* 'P5FS' */
#define P5FS_VERSION            1
#define P5FS_BLOCK              512
#define P5FS_INODE_SIZE         64
#define P5FS_INODES_PER_BLOCK   (P5FS_BLOCK / P5FS_INODE_SIZE)
#define P5FS_DIRENT_SIZE        32
#define P5FS_DIRENTS_PER_BLOCK  (P5FS_BLOCK / P5FS_DIRENT_SIZE)
#define P5FS_NAME_MAX           27
#define P5FS_NDIRECT            12
#define P5FS_PTRS_PER_BLOCK     (P5FS_BLOCK / 4)
#define P5FS_MAX_FILE_BLOCKS    (P5FS_NDIRECT + P5FS_PTRS_PER_BLOCK)
#define P5FS_MAX_FILE_SIZE      (P5FS_MAX_FILE_BLOCKS * P5FS_BLOCK)    /* 70 KB */
#define P5FS_ROOT_INO           1
#define P5FS_PART_TYPE          0x7F            /* MBR partition type */
#define P5FS_CHECKSUM_SALT      0x5035F5A5u
#define P5FS_MAX_BITMAP_BLOCKS  64              /* => max 128 MB file system */

enum { P5FS_T_FREE = 0, P5FS_T_FILE = 1, P5FS_T_DIR = 2 };

struct p5fs_super {
    u32 magic;
    u32 version;
    u32 total_blocks;
    u32 bitmap_start;
    u32 bitmap_blocks;
    u32 inode_start;
    u32 inode_blocks;
    u32 inode_count;
    u32 data_start;
    u32 root_inode;
    char label[16];
    u32 checksum;           /* sum of the 14 words above + salt */
} PACKED;

struct p5fs_inode {
    u16 type;
    u16 links;
    u32 size;
    u32 mtime;              /* seconds since 1970-01-01 (CMOS RTC) */
    u32 direct[P5FS_NDIRECT];
    u32 indirect;
} PACKED;

struct p5fs_dirent {
    u32 inode;              /* 0 = unused slot */
    char name[P5FS_NAME_MAX + 1];
} PACKED;

STATIC_ASSERT(sizeof(struct p5fs_super) == 60, "superblock layout");
STATIC_ASSERT(sizeof(struct p5fs_inode) == P5FS_INODE_SIZE, "inode layout");
STATIC_ASSERT(sizeof(struct p5fs_dirent) == P5FS_DIRENT_SIZE, "dirent layout");
#endif
