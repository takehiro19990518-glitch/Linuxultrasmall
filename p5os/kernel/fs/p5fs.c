/* P5FS file system driver.
 *
 * Simplicity and metadata safety over speed:
 *  - every change is written through to disk immediately (no write cache);
 *  - file contents are replaced copy-on-write: new blocks are allocated and
 *    written, the allocation bitmap is flushed, only then is the inode
 *    updated to point at them, and the old blocks are freed last.  A crash
 *    or I/O error at any point leaves at worst leaked blocks or an
 *    unreferenced inode (reported by 'fsck'), never a block owned twice or
 *    an inode pointing at free blocks;
 *  - the superblock (block 0) is never written after mkfs;
 *  - all writes are bounds-checked against the partition.
 */
#include <fs.h>
#include <p5fs.h>
#include <ata.h>
#include <errno.h>
#include <mm.h>
#include <string.h>
#include <klog.h>
#include <printf.h>
#include <rtc.h>

#define MBR_SIG_OFFSET      510
#define MBR_PART_OFFSET     446
#define MBR_PART_ENTRIES    4
#define MBR_PART_SIZE       16

static struct {
    bool mounted;
    struct ata_device *dev;
    u32 start;                  /* first LBA of the file system */
    struct p5fs_super sb;
    u8 *bitmap;                 /* in-memory copy of the allocation bitmap */
    u8 *dirty;                  /* per bitmap block: needs writing */
    u32 free_blocks;
    char cwd[FS_PATH_MAX];
} fs;

/* ---- block I/O ----------------------------------------------------------- */

static int bread(u32 blk, void *buf)
{
    if (blk >= fs.sb.total_blocks)
        return E_RANGE;
    return ata_read(fs.dev, fs.start + blk, 1, buf);
}

static int bwrite(u32 blk, const void *buf)
{
    if (blk == 0 || blk >= fs.sb.total_blocks) {    /* never the superblock */
        LOG_ERR("p5fs: refused write to block %u", blk);
        return E_RANGE;
    }
    return ata_write(fs.dev, fs.start + blk, 1, buf);
}

/* ---- allocation bitmap ----------------------------------------------------- */

static bool bm_test(u32 b) { return fs.bitmap[b >> 3] & (1u << (b & 7)); }

static void bm_set(u32 b, bool used)
{
    if (used)
        fs.bitmap[b >> 3] |= (u8)(1u << (b & 7));
    else
        fs.bitmap[b >> 3] &= (u8)~(1u << (b & 7));
    fs.dirty[b / (P5FS_BLOCK * 8)] = 1;
}

static int bm_flush(void)
{
    int rc = E_OK;
    for (u32 i = 0; i < fs.sb.bitmap_blocks; i++) {
        if (!fs.dirty[i])
            continue;
        int r = bwrite(fs.sb.bitmap_start + i, fs.bitmap + i * P5FS_BLOCK);
        if (r)
            rc = r;
        else
            fs.dirty[i] = 0;
    }
    return rc;
}

static bool is_data_block(u32 b)
{
    return b >= fs.sb.data_start && b < fs.sb.total_blocks;
}

/* Reserve n free data blocks in memory (not yet flushed). */
static int alloc_blocks(u32 n, u32 *out)
{
    if (n > fs.free_blocks)
        return E_NOSPC;
    u32 got = 0;
    for (u32 b = fs.sb.data_start; b < fs.sb.total_blocks && got < n; b++)
        if (!bm_test(b)) {
            bm_set(b, true);
            out[got++] = b;
        }
    if (got < n) {                      /* bitmap disagrees with the counter */
        for (u32 i = 0; i < got; i++)
            bm_set(out[i], false);
        return E_CORRUPT;
    }
    fs.free_blocks -= n;
    return E_OK;
}

static void release_blocks(const u32 *list, u32 n)
{
    for (u32 i = 0; i < n; i++)
        if (is_data_block(list[i]) && bm_test(list[i])) {
            bm_set(list[i], false);
            fs.free_blocks++;
        }
}

/* ---- inodes ------------------------------------------------------------------ */

static int iread(u32 ino, struct p5fs_inode *out)
{
    u8 b[P5FS_BLOCK];
    if (ino < 1 || ino > fs.sb.inode_count)
        return E_CORRUPT;
    int rc = bread(fs.sb.inode_start + (ino - 1) / P5FS_INODES_PER_BLOCK, b);
    if (rc)
        return rc;
    memcpy(out, b + ((ino - 1) % P5FS_INODES_PER_BLOCK) * P5FS_INODE_SIZE, sizeof(*out));
    if (out->type > P5FS_T_DIR || out->size > P5FS_MAX_FILE_SIZE)
        return E_CORRUPT;
    return E_OK;
}

static int iwrite(u32 ino, const struct p5fs_inode *in)
{
    u8 b[P5FS_BLOCK];
    if (ino < 1 || ino > fs.sb.inode_count)
        return E_CORRUPT;
    u32 blk = fs.sb.inode_start + (ino - 1) / P5FS_INODES_PER_BLOCK;
    int rc = bread(blk, b);
    if (rc)
        return rc;
    memcpy(b + ((ino - 1) % P5FS_INODES_PER_BLOCK) * P5FS_INODE_SIZE, in, sizeof(*in));
    return bwrite(blk, b);
}

static int alloc_inode(u32 *ino_out)
{
    u8 b[P5FS_BLOCK];
    for (u32 blk = 0; blk < fs.sb.inode_blocks; blk++) {
        int rc = bread(fs.sb.inode_start + blk, b);
        if (rc)
            return rc;
        for (u32 i = 0; i < P5FS_INODES_PER_BLOCK; i++) {
            const struct p5fs_inode *n = (const void *)(b + i * P5FS_INODE_SIZE);
            u32 ino = blk * P5FS_INODES_PER_BLOCK + i + 1;
            if (n->type == P5FS_T_FREE && ino <= fs.sb.inode_count) {
                *ino_out = ino;
                return E_OK;
            }
        }
    }
    return E_NOSPC;
}

/* List of data blocks of an inode, validated.  list must hold
 * P5FS_MAX_FILE_BLOCKS entries. */
static int inode_blocks(const struct p5fs_inode *n, u32 *list, u32 *count)
{
    u32 need = (n->size + P5FS_BLOCK - 1) / P5FS_BLOCK, c = 0;
    if (need > P5FS_MAX_FILE_BLOCKS)
        return E_CORRUPT;
    for (u32 i = 0; i < P5FS_NDIRECT && c < need; i++)
        list[c++] = n->direct[i];
    if (c < need) {
        u32 ptrs[P5FS_PTRS_PER_BLOCK];
        if (!is_data_block(n->indirect))
            return E_CORRUPT;
        int rc = bread(n->indirect, ptrs);
        if (rc)
            return rc;
        for (u32 i = 0; c < need; i++)
            list[c++] = ptrs[i];
    }
    for (u32 i = 0; i < c; i++)
        if (!is_data_block(list[i]))
            return E_CORRUPT;
    *count = c;
    return E_OK;
}

/* Read the whole content of an inode into a new heap buffer (+1 byte, NUL
 * terminated for convenience). */
static int inode_read_all(const struct p5fs_inode *n, u8 **out)
{
    u32 list[P5FS_MAX_FILE_BLOCKS], cnt;
    int rc = inode_blocks(n, list, &cnt);
    if (rc)
        return rc;
    u8 *buf = kmalloc(cnt * P5FS_BLOCK + 1);
    if (!buf)
        return E_NOMEM;
    for (u32 i = 0; i < cnt; i++) {
        rc = bread(list[i], buf + i * P5FS_BLOCK);
        if (rc) {
            kfree(buf);
            return rc;
        }
    }
    buf[n->size] = '\0';
    *out = buf;
    return E_OK;
}

/* Copy-on-write replacement of an inode's content (see header comment).
 * 'n' holds the current inode (type set; zeroed for a new inode) and is
 * updated on success. */
static int inode_set_data(u32 ino, struct p5fs_inode *n, const void *data, u32 len)
{
    u32 old_list[P5FS_MAX_FILE_BLOCKS + 1], old_cnt = 0;
    u32 new_list[P5FS_MAX_FILE_BLOCKS + 1];
    u8 b[P5FS_BLOCK];
    int rc;

    if (len > P5FS_MAX_FILE_SIZE)
        return E_FBIG;
    if (n->size) {
        rc = inode_blocks(n, old_list, &old_cnt);
        if (rc)
            return rc;
        if (old_cnt > P5FS_NDIRECT)
            old_list[old_cnt++] = n->indirect;
    }
    u32 nblk = (len + P5FS_BLOCK - 1) / P5FS_BLOCK;
    u32 need = nblk + (nblk > P5FS_NDIRECT ? 1 : 0);
    rc = alloc_blocks(need, new_list);
    if (rc)
        return rc;

    for (u32 i = 0; i < nblk && !rc; i++) {
        u32 off = i * P5FS_BLOCK, chunk = MIN(len - off, (u32)P5FS_BLOCK);
        memset(b, 0, sizeof(b));
        memcpy(b, (const u8 *)data + off, chunk);
        rc = bwrite(new_list[i], b);
    }
    struct p5fs_inode nn;
    memset(&nn, 0, sizeof(nn));
    nn.type = n->type;
    nn.links = 1;
    nn.size = len;
    nn.mtime = rtc_unix_time();
    for (u32 i = 0; i < nblk && i < P5FS_NDIRECT; i++)
        nn.direct[i] = new_list[i];
    if (!rc && nblk > P5FS_NDIRECT) {
        u32 ptrs[P5FS_PTRS_PER_BLOCK];
        memset(ptrs, 0, sizeof(ptrs));
        for (u32 i = P5FS_NDIRECT; i < nblk; i++)
            ptrs[i - P5FS_NDIRECT] = new_list[i];
        nn.indirect = new_list[nblk];
        rc = bwrite(nn.indirect, ptrs);
    }
    if (!rc)
        rc = bm_flush();            /* new blocks are now "used" on disk */
    if (!rc)
        rc = iwrite(ino, &nn);      /* the switch-over point */
    if (rc) {
        release_blocks(new_list, need);
        bm_flush();
        return rc;
    }
    *n = nn;
    release_blocks(old_list, old_cnt);
    int frc = bm_flush();
    if (frc)
        LOG_WARN("p5fs: could not free old blocks of inode %u (leaked)", ino);
    return E_OK;
}

/* ---- directories ------------------------------------------------------------- */

struct dir_slot {
    u32 block;                  /* disk block holding the entry */
    u32 index;                  /* entry index within the block */
};

/* Find 'name' in directory 'dir'.  E_NOENT if absent. */
static int dir_lookup(u32 dir, const char *name, u32 *ino, struct dir_slot *slot)
{
    struct p5fs_inode n;
    u32 list[P5FS_MAX_FILE_BLOCKS], cnt;
    struct p5fs_dirent ents[P5FS_DIRENTS_PER_BLOCK];
    int rc = iread(dir, &n);
    if (rc)
        return rc;
    if (n.type != P5FS_T_DIR)
        return E_NOTDIR;
    rc = inode_blocks(&n, list, &cnt);
    if (rc)
        return rc;
    for (u32 i = 0; i < cnt; i++) {
        rc = bread(list[i], ents);
        if (rc)
            return rc;
        for (u32 j = 0; j < P5FS_DIRENTS_PER_BLOCK; j++) {
            if (!ents[j].inode)
                continue;
            ents[j].name[P5FS_NAME_MAX] = '\0';
            if (!strcmp(ents[j].name, name)) {
                *ino = ents[j].inode;
                if (slot) {
                    slot->block = list[i];
                    slot->index = j;
                }
                return E_OK;
            }
        }
    }
    return E_NOENT;
}

typedef bool (*dirent_cb)(const struct p5fs_dirent *d, void *ctx);

static int dir_iterate(u32 dir, dirent_cb cb, void *ctx)
{
    struct p5fs_inode n;
    u32 list[P5FS_MAX_FILE_BLOCKS], cnt;
    struct p5fs_dirent ents[P5FS_DIRENTS_PER_BLOCK];
    int rc = iread(dir, &n);
    if (rc)
        return rc;
    if (n.type != P5FS_T_DIR)
        return E_NOTDIR;
    rc = inode_blocks(&n, list, &cnt);
    if (rc)
        return rc;
    for (u32 i = 0; i < cnt; i++) {
        rc = bread(list[i], ents);
        if (rc)
            return rc;
        for (u32 j = 0; j < P5FS_DIRENTS_PER_BLOCK; j++) {
            if (!ents[j].inode)
                continue;
            ents[j].name[P5FS_NAME_MAX] = '\0';
            if (!cb(&ents[j], ctx))
                return E_OK;
        }
    }
    return E_OK;
}

static int dir_add(u32 dir, const char *name, u32 ino)
{
    struct p5fs_inode n;
    u32 list[P5FS_MAX_FILE_BLOCKS], cnt;
    struct p5fs_dirent ents[P5FS_DIRENTS_PER_BLOCK];
    int rc = iread(dir, &n);
    if (rc)
        return rc;
    if (n.type != P5FS_T_DIR)
        return E_NOTDIR;
    rc = inode_blocks(&n, list, &cnt);
    if (rc)
        return rc;
    /* reuse a free slot if there is one */
    for (u32 i = 0; i < cnt; i++) {
        rc = bread(list[i], ents);
        if (rc)
            return rc;
        for (u32 j = 0; j < P5FS_DIRENTS_PER_BLOCK; j++) {
            if (ents[j].inode)
                continue;
            memset(&ents[j], 0, sizeof(ents[j]));
            ents[j].inode = ino;
            strlcpy(ents[j].name, name, sizeof(ents[j].name));
            return bwrite(list[i], ents);
        }
    }
    /* directory full: grow it by one block (copy-on-write) */
    if (cnt >= P5FS_MAX_FILE_BLOCKS)
        return E_NOSPC;
    u8 *data;
    rc = inode_read_all(&n, &data);
    if (rc)
        return rc;
    u8 *grown = kmalloc(n.size + P5FS_BLOCK);
    if (!grown) {
        kfree(data);
        return E_NOMEM;
    }
    memcpy(grown, data, n.size);
    kfree(data);
    memset(grown + n.size, 0, P5FS_BLOCK);
    struct p5fs_dirent *e = (struct p5fs_dirent *)(grown + n.size);
    e->inode = ino;
    strlcpy(e->name, name, sizeof(e->name));
    rc = inode_set_data(dir, &n, grown, n.size + P5FS_BLOCK);
    kfree(grown);
    return rc;
}

static int dir_clear_slot(const struct dir_slot *s)
{
    struct p5fs_dirent ents[P5FS_DIRENTS_PER_BLOCK];
    int rc = bread(s->block, ents);
    if (rc)
        return rc;
    memset(&ents[s->index], 0, sizeof(ents[0]));
    return bwrite(s->block, ents);
}

static bool not_dot(const char *n) { return strcmp(n, ".") && strcmp(n, ".."); }

static bool count_entries_cb(const struct p5fs_dirent *d, void *ctx)
{
    if (not_dot(d->name))
        (*(u32 *)ctx)++;
    return true;
}

/* ---- paths ----------------------------------------------------------------- */

/* Combine cwd and 'path' into a normalized absolute path in 'out'. */
static int normalize(const char *path, char *out)
{
    char tmp[FS_PATH_MAX * 2];
    if (!path || !*path)
        return E_INVAL;
    if (path[0] == '/')
        strlcpy(tmp, path, sizeof(tmp));
    else {
        strlcpy(tmp, fs.cwd, sizeof(tmp));
        strlcat(tmp, "/", sizeof(tmp));
        if (strlcat(tmp, path, sizeof(tmp)) >= sizeof(tmp))
            return E_NAMETOOLONG;
    }
    u32 olen = 0;
    out[0] = '\0';
    char *p = tmp;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        char *comp = p;
        while (*p && *p != '/')
            p++;
        u32 clen = (u32)(p - comp);
        if (clen == 1 && comp[0] == '.')
            continue;
        if (clen == 2 && comp[0] == '.' && comp[1] == '.') {
            while (olen > 0 && out[olen - 1] != '/')
                olen--;
            if (olen > 0)
                olen--;                 /* drop the separator too */
            out[olen] = '\0';
            continue;
        }
        if (clen > P5FS_NAME_MAX)
            return E_NAMETOOLONG;
        if (olen + 1 + clen + 1 > FS_PATH_MAX)
            return E_NAMETOOLONG;
        out[olen++] = '/';
        memcpy(out + olen, comp, clen);
        olen += clen;
        out[olen] = '\0';
    }
    if (olen == 0)
        strlcpy(out, "/", FS_PATH_MAX);
    return E_OK;
}

/* Walk a normalized absolute path from the root directory. */
static int walk(const char *abs, u32 *ino)
{
    char name[P5FS_NAME_MAX + 1];
    u32 cur = fs.sb.root_inode;
    const char *p = abs;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        u32 n = 0;
        while (*p && *p != '/')
            name[n++] = *p++;
        name[n] = '\0';
        int rc = dir_lookup(cur, name, &cur, NULL);
        if (rc)
            return rc;
    }
    *ino = cur;
    return E_OK;
}

static int resolve(const char *path, u32 *ino)
{
    char abs[FS_PATH_MAX];
    int rc = normalize(path, abs);
    return rc ? rc : walk(abs, ino);
}

/* Split into parent directory inode and final name component. */
static int resolve_parent(const char *path, u32 *parent, char *name)
{
    char abs[FS_PATH_MAX];
    int rc = normalize(path, abs);
    if (rc)
        return rc;
    if (!strcmp(abs, "/"))
        return E_INVAL;
    char *slash = strrchr(abs, '/');
    strlcpy(name, slash + 1, P5FS_NAME_MAX + 1);
    if (slash == abs)
        slash[1] = '\0';                /* parent is the root */
    else
        *slash = '\0';
    rc = walk(abs, parent);
    if (rc)
        return rc;
    struct p5fs_inode n;
    rc = iread(*parent, &n);
    if (rc)
        return rc;
    return n.type == P5FS_T_DIR ? E_OK : E_NOTDIR;
}

static int check_name(const char *name)
{
    if (!*name || !not_dot(name))
        return E_INVAL;
    if (strlen(name) > P5FS_NAME_MAX)
        return E_NAMETOOLONG;
    for (const char *c = name; *c; c++)
        if (*c < 0x20 || *c > 0x7E || *c == '/')
            return E_INVAL;
    return E_OK;
}

/* ---- mounting --------------------------------------------------------------- */

static u32 super_checksum(const struct p5fs_super *sb)
{
    u32 w[14], sum = 0;
    memcpy(w, sb, sizeof(w));           /* struct is packed: copy, don't cast */
    for (u32 i = 0; i < 14; i++)
        sum += w[i];
    return sum + P5FS_CHECKSUM_SALT;
}

static const char *validate_super(const struct p5fs_super *sb, u32 avail)
{
    if (sb->magic != P5FS_MAGIC)
        return "bad magic";
    if (sb->version != P5FS_VERSION)
        return "unsupported version";
    if (sb->checksum != super_checksum(sb))
        return "checksum mismatch";
    if (sb->total_blocks > avail || sb->total_blocks < 16)
        return "size does not fit the partition";
    if (sb->bitmap_start != 1 ||
        sb->bitmap_blocks != (sb->total_blocks + P5FS_BLOCK * 8 - 1) / (P5FS_BLOCK * 8) ||
        sb->bitmap_blocks > P5FS_MAX_BITMAP_BLOCKS)
        return "bad bitmap location";
    if (sb->inode_start != sb->bitmap_start + sb->bitmap_blocks ||
        sb->inode_count != sb->inode_blocks * P5FS_INODES_PER_BLOCK || !sb->inode_count)
        return "bad inode table";
    if (sb->data_start != sb->inode_start + sb->inode_blocks ||
        sb->data_start >= sb->total_blocks)
        return "bad data area";
    if (sb->root_inode != P5FS_ROOT_INO)
        return "bad root inode";
    return NULL;
}

static int try_mount(struct ata_device *d, u32 start, u32 sectors)
{
    u8 b[P5FS_BLOCK];
    int rc = ata_read(d, start, 1, b);
    if (rc)
        return rc;
    struct p5fs_super sb;
    memcpy(&sb, b, sizeof(sb));
    if (sb.magic != P5FS_MAGIC)
        return E_NODEV;
    const char *why = validate_super(&sb, sectors);
    if (why) {
        LOG_ERR("p5fs: %s LBA %u: superblock invalid (%s), not mounting", d->name, start, why);
        return E_CORRUPT;
    }
    u8 *bitmap = kmalloc(sb.bitmap_blocks * P5FS_BLOCK);
    u8 *dirty = kzalloc(sb.bitmap_blocks);
    if (!bitmap || !dirty) {
        kfree(bitmap);
        kfree(dirty);
        return E_NOMEM;
    }
    rc = ata_read(d, start + sb.bitmap_start, sb.bitmap_blocks, bitmap);
    if (rc) {
        kfree(bitmap);
        kfree(dirty);
        return rc;
    }
    fs.dev = d;
    fs.start = start;
    fs.sb = sb;
    fs.bitmap = bitmap;
    fs.dirty = dirty;
    fs.free_blocks = 0;
    for (u32 blk = sb.data_start; blk < sb.total_blocks; blk++)
        if (!bm_test(blk))
            fs.free_blocks++;
    /* metadata blocks must be marked in use */
    for (u32 blk = 0; blk < sb.data_start; blk++)
        if (!bm_test(blk)) {
            LOG_ERR("p5fs: metadata block %u marked free - refusing to mount", blk);
            kfree(bitmap);
            kfree(dirty);
            return E_CORRUPT;
        }
    struct p5fs_inode root;
    rc = iread(sb.root_inode, &root);
    if (rc || root.type != P5FS_T_DIR) {
        LOG_ERR("p5fs: root directory invalid - refusing to mount");
        kfree(bitmap);
        kfree(dirty);
        return rc ? rc : E_CORRUPT;
    }
    strlcpy(fs.cwd, "/", sizeof(fs.cwd));
    fs.mounted = true;
    char label[17];
    memcpy(label, sb.label, 16);
    label[16] = '\0';
    LOG_INFO("p5fs: mounted %s at LBA %u, label \"%s\", %u blocks (%u free), %u inodes",
             d->name, start, label, sb.total_blocks, fs.free_blocks, sb.inode_count);
    return E_OK;
}

int fs_mount_auto(void)
{
    u8 mbr[P5FS_BLOCK];
    for (u32 i = 0; i < ATA_MAX_DEVICES; i++) {
        struct ata_device *d = ata_get(i);
        if (!d)
            continue;
        int rc = try_mount(d, 0, d->sectors);         /* whole-disk P5FS */
        if (rc == E_OK)
            return E_OK;
        if (ata_read(d, 0, 1, mbr))
            continue;
        if (mbr[MBR_SIG_OFFSET] != 0x55 || mbr[MBR_SIG_OFFSET + 1] != 0xAA)
            continue;
        for (u32 p = 0; p < MBR_PART_ENTRIES; p++) {
            const u8 *e = mbr + MBR_PART_OFFSET + p * MBR_PART_SIZE;
            u32 start, size;
            memcpy(&start, e + 8, 4);
            memcpy(&size, e + 12, 4);
            if (e[4] != P5FS_PART_TYPE || !start || !size || start >= d->sectors ||
                size > d->sectors - start)
                continue;
            if (try_mount(d, start, size) == E_OK)
                return E_OK;
        }
    }
    return E_NODEV;
}

bool fs_is_mounted(void) { return fs.mounted; }
const char *fs_cwd(void) { return fs.mounted ? fs.cwd : NULL; }

/* ---- public operations --------------------------------------------------------- */

int fs_chdir(const char *path)
{
    char abs[FS_PATH_MAX];
    u32 ino;
    struct p5fs_inode n;
    if (!fs.mounted)
        return E_NODEV;
    int rc = normalize(path, abs);
    if (!rc)
        rc = walk(abs, &ino);
    if (!rc)
        rc = iread(ino, &n);
    if (rc)
        return rc;
    if (n.type != P5FS_T_DIR)
        return E_NOTDIR;
    strlcpy(fs.cwd, abs, sizeof(fs.cwd));
    return E_OK;
}

static void fill_entry(struct fs_entry *e, const char *name, u32 ino, const struct p5fs_inode *n)
{
    strlcpy(e->name, name, sizeof(e->name));
    e->ino = ino;
    e->type = n->type;
    e->size = n->size;
    e->mtime = n->mtime;
}

int fs_stat(const char *path, struct fs_entry *e)
{
    char abs[FS_PATH_MAX];
    u32 ino;
    struct p5fs_inode n;
    if (!fs.mounted)
        return E_NODEV;
    int rc = normalize(path, abs);
    if (!rc)
        rc = walk(abs, &ino);
    if (!rc)
        rc = iread(ino, &n);
    if (rc)
        return rc;
    const char *slash = strrchr(abs, '/');
    fill_entry(e, slash[1] ? slash + 1 : "/", ino, &n);
    return E_OK;
}

struct list_ctx {
    fs_list_cb cb;
    void *ctx;
    int rc;
};

static bool list_cb(const struct p5fs_dirent *d, void *c)
{
    struct list_ctx *lc = c;
    struct p5fs_inode n;
    struct fs_entry e;
    int rc = iread(d->inode, &n);
    if (rc) {
        lc->rc = rc;
        return false;
    }
    fill_entry(&e, d->name, d->inode, &n);
    return lc->cb(&e, lc->ctx);
}

int fs_list(const char *path, fs_list_cb cb, void *ctx)
{
    u32 ino;
    if (!fs.mounted)
        return E_NODEV;
    int rc = resolve(path, &ino);
    if (rc)
        return rc;
    struct list_ctx lc = { cb, ctx, E_OK };
    rc = dir_iterate(ino, list_cb, &lc);
    return rc ? rc : lc.rc;
}

int fs_read_file(const char *path, void **data, u32 *len)
{
    u32 ino;
    struct p5fs_inode n;
    if (!fs.mounted)
        return E_NODEV;
    int rc = resolve(path, &ino);
    if (!rc)
        rc = iread(ino, &n);
    if (rc)
        return rc;
    if (n.type == P5FS_T_DIR)
        return E_ISDIR;
    u8 *buf;
    rc = inode_read_all(&n, &buf);
    if (rc)
        return rc;
    *data = buf;
    *len = n.size;
    return E_OK;
}

int fs_write_file(const char *path, const void *data, u32 len, bool append)
{
    char name[P5FS_NAME_MAX + 1];
    u32 parent, ino;
    struct p5fs_inode n;
    u8 *merged = NULL;
    if (!fs.mounted)
        return E_NODEV;
    int rc = resolve_parent(path, &parent, name);
    if (rc)
        return rc;
    rc = check_name(name);
    if (rc)
        return rc;
    rc = dir_lookup(parent, name, &ino, NULL);
    bool exists = rc == E_OK;
    if (rc && rc != E_NOENT)
        return rc;

    if (exists) {
        rc = iread(ino, &n);
        if (rc)
            return rc;
        if (n.type != P5FS_T_FILE)
            return E_ISDIR;
        if (append && n.size) {
            if (n.size + len > P5FS_MAX_FILE_SIZE)
                return E_FBIG;
            rc = inode_read_all(&n, &merged);
            if (rc)
                return rc;
            u8 *grown = kmalloc(n.size + len + 1);
            if (!grown) {
                kfree(merged);
                return E_NOMEM;
            }
            memcpy(grown, merged, n.size);
            memcpy(grown + n.size, data, len);
            kfree(merged);
            merged = grown;
            data = merged;
            len += n.size;
        }
        rc = inode_set_data(ino, &n, data, len);
        kfree(merged);
        return rc;
    }

    if (len > P5FS_MAX_FILE_SIZE)
        return E_FBIG;
    rc = alloc_inode(&ino);
    if (rc)
        return rc;
    memset(&n, 0, sizeof(n));
    n.type = P5FS_T_FILE;
    rc = inode_set_data(ino, &n, data, len);    /* writes the inode */
    if (rc)
        return rc;
    rc = dir_add(parent, name, ino);
    if (rc) {                                   /* undo: free inode + blocks */
        struct p5fs_inode empty;
        memset(&empty, 0, sizeof(empty));
        inode_set_data(ino, &n, NULL, 0);
        iwrite(ino, &empty);
    }
    return rc;
}

int fs_mkdir(const char *path)
{
    char name[P5FS_NAME_MAX + 1];
    u32 parent, ino, existing;
    struct p5fs_inode n;
    struct p5fs_dirent ents[P5FS_DIRENTS_PER_BLOCK];
    if (!fs.mounted)
        return E_NODEV;
    int rc = resolve_parent(path, &parent, name);
    if (rc)
        return rc;
    rc = check_name(name);
    if (rc)
        return rc;
    rc = dir_lookup(parent, name, &existing, NULL);
    if (rc == E_OK)
        return E_EXIST;
    if (rc != E_NOENT)
        return rc;
    rc = alloc_inode(&ino);
    if (rc)
        return rc;
    memset(ents, 0, sizeof(ents));
    ents[0].inode = ino;
    strlcpy(ents[0].name, ".", sizeof(ents[0].name));
    ents[1].inode = parent;
    strlcpy(ents[1].name, "..", sizeof(ents[1].name));
    memset(&n, 0, sizeof(n));
    n.type = P5FS_T_DIR;
    rc = inode_set_data(ino, &n, ents, sizeof(ents));
    if (rc)
        return rc;
    rc = dir_add(parent, name, ino);
    if (rc) {
        struct p5fs_inode empty;
        memset(&empty, 0, sizeof(empty));
        n.type = P5FS_T_FILE;
        inode_set_data(ino, &n, NULL, 0);
        iwrite(ino, &empty);
    }
    return rc;
}

int fs_remove(const char *path)
{
    char name[P5FS_NAME_MAX + 1];
    u32 parent, ino;
    struct dir_slot slot;
    struct p5fs_inode n;
    if (!fs.mounted)
        return E_NODEV;
    int rc = resolve_parent(path, &parent, name);
    if (rc)
        return rc;
    if (!not_dot(name))
        return E_INVAL;
    rc = dir_lookup(parent, name, &ino, &slot);
    if (!rc)
        rc = iread(ino, &n);
    if (rc)
        return rc;
    if (n.type == P5FS_T_DIR) {
        u32 entries = 0;
        rc = dir_iterate(ino, count_entries_cb, &entries);
        if (rc)
            return rc;
        if (entries)
            return E_NOTEMPTY;
        u32 cwd_ino;
        if (!walk(fs.cwd, &cwd_ino) && cwd_ino == ino)
            return E_BUSY;              /* cannot remove the current directory */
    }
    u32 list[P5FS_MAX_FILE_BLOCKS + 1], cnt = 0;
    if (n.size) {
        rc = inode_blocks(&n, list, &cnt);
        if (rc)
            return rc;
        if (cnt > P5FS_NDIRECT)
            list[cnt++] = n.indirect;
    }
    /* 1) unlink, 2) free the inode, 3) free the blocks */
    rc = dir_clear_slot(&slot);
    if (rc)
        return rc;
    struct p5fs_inode empty;
    memset(&empty, 0, sizeof(empty));
    rc = iwrite(ino, &empty);
    if (rc)
        return rc;
    release_blocks(list, cnt);
    return bm_flush();
}

int fs_info(struct fs_info *out)
{
    u8 b[P5FS_BLOCK];
    if (!fs.mounted)
        return E_NODEV;
    memset(out, 0, sizeof(*out));
    memcpy(out->label, fs.sb.label, 16);
    strlcpy(out->device, fs.dev->name, sizeof(out->device));
    out->part_start = fs.start;
    out->total_blocks = fs.sb.total_blocks;
    out->free_blocks = fs.free_blocks;
    out->data_blocks = fs.sb.total_blocks - fs.sb.data_start;
    out->inode_count = fs.sb.inode_count;
    for (u32 blk = 0; blk < fs.sb.inode_blocks; blk++) {
        int rc = bread(fs.sb.inode_start + blk, b);
        if (rc)
            return rc;
        for (u32 i = 0; i < P5FS_INODES_PER_BLOCK; i++)
            if (((const struct p5fs_inode *)(b + i * P5FS_INODE_SIZE))->type != P5FS_T_FREE)
                out->inodes_used++;
    }
    return E_OK;
}

/* ---- consistency check (read only) ------------------------------------------------ */

struct check_ctx {
    u32 dir;
    u8 *referenced;
    u32 problems;
    bool verbose;
};

#define FSCK_MAX_REPORTED 20

static void problem(struct check_ctx *c, const char *fmt, ...) PRINTF_FMT(2, 3);
static void problem(struct check_ctx *c, const char *fmt, ...)
{
    char msg[120];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    c->problems++;
    LOG_WARN("fsck: %s", msg);
    if (c->verbose && c->problems <= FSCK_MAX_REPORTED)
        kprintf("  %s\n", msg);
}

static bool check_dir_cb(const struct p5fs_dirent *d, void *v)
{
    struct check_ctx *c = v;
    struct p5fs_inode n;
    if (d->inode < 1 || d->inode > fs.sb.inode_count) {
        problem(c, "dir %u: entry points to invalid inode %u", c->dir, d->inode);
        return true;
    }
    if (!strcmp(d->name, ".")) {
        if (d->inode != c->dir)
            problem(c, "dir %u: '.' points to %u", c->dir, d->inode);
        return true;
    }
    if (!strcmp(d->name, ".."))
        return true;
    if (iread(d->inode, &n) || n.type == P5FS_T_FREE)
        problem(c, "dir %u: entry points to free/unreadable inode %u", c->dir, d->inode);
    if (c->referenced[d->inode >> 3] & (1u << (d->inode & 7)))
        problem(c, "inode %u is linked more than once (dir %u)", d->inode, c->dir);
    c->referenced[d->inode >> 3] |= (u8)(1u << (d->inode & 7));
    return true;
}

int fs_check(bool verbose)
{
    if (!fs.mounted)
        return E_NODEV;
    u32 bm_bytes = fs.sb.bitmap_blocks * P5FS_BLOCK;
    u8 *seen = kzalloc(bm_bytes);
    struct check_ctx c = { 0, kzalloc(fs.sb.inode_count / 8 + 1), 0, verbose };
    if (!seen || !c.referenced) {
        kfree(seen);
        kfree(c.referenced);
        return E_NOMEM;
    }
    u32 files = 0, dirs = 0;
    for (u32 b = 0; b < fs.sb.data_start; b++)
        seen[b >> 3] |= (u8)(1u << (b & 7));
    for (u32 b = fs.sb.total_blocks; b < bm_bytes * 8; b++)
        seen[b >> 3] |= (u8)(1u << (b & 7));
    c.referenced[fs.sb.root_inode >> 3] |= (u8)(1u << (fs.sb.root_inode & 7));

    for (u32 ino = 1; ino <= fs.sb.inode_count; ino++) {
        struct p5fs_inode n;
        u32 list[P5FS_MAX_FILE_BLOCKS + 1], cnt;
        int rc = iread(ino, &n);
        if (rc == E_CORRUPT) {
            problem(&c, "inode %u: invalid type or size", ino);
            continue;
        }
        if (rc) {
            kfree(seen);
            kfree(c.referenced);
            return rc;
        }
        if (n.type == P5FS_T_FREE)
            continue;
        n.type == P5FS_T_DIR ? dirs++ : files++;
        if (inode_blocks(&n, list, &cnt)) {
            problem(&c, "inode %u: block pointer outside data area", ino);
            continue;
        }
        if (cnt > P5FS_NDIRECT)
            list[cnt++] = n.indirect;
        for (u32 i = 0; i < cnt; i++) {
            u32 b = list[i];
            if (seen[b >> 3] & (1u << (b & 7)))
                problem(&c, "block %u claimed twice (inode %u)", b, ino);
            seen[b >> 3] |= (u8)(1u << (b & 7));
        }
        if (n.type == P5FS_T_DIR) {
            c.dir = ino;
            if (dir_iterate(ino, check_dir_cb, &c))
                problem(&c, "dir %u: unreadable", ino);
        }
    }
    for (u32 ino = 1; ino <= fs.sb.inode_count; ino++) {
        struct p5fs_inode n;
        if (iread(ino, &n) || n.type == P5FS_T_FREE)
            continue;
        if (!(c.referenced[ino >> 3] & (1u << (ino & 7))))
            problem(&c, "inode %u is not in any directory (orphan)", ino);
    }
    u32 leaked = 0, unmarked = 0;
    for (u32 b = 0; b < fs.sb.total_blocks; b++) {
        bool in_use = seen[b >> 3] & (1u << (b & 7));
        if (in_use && !bm_test(b))
            unmarked++;
        else if (!in_use && bm_test(b))
            leaked++;
    }
    if (unmarked)
        problem(&c, "%u used block(s) marked free in the bitmap", unmarked);
    if (leaked)
        problem(&c, "%u block(s) marked used but not owned (leaked)", leaked);
    if (verbose)
        kprintf("fsck: %u files, %u directories, %u/%u blocks free: %s (%u problem(s))\n",
                files, dirs, fs.free_blocks, fs.sb.total_blocks - fs.sb.data_start,
                c.problems ? "ERRORS" : "clean", c.problems);
    kfree(seen);
    kfree(c.referenced);
    return (int)c.problems;
}
