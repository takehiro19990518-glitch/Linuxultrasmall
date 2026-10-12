/* Storage commands: ls cat write append mkdir rm rmdir cd pwd df fsck
 * disks sector date */
#include <shell.h>
#include <fs.h>
#include <p5fs.h>
#include <ata.h>
#include <errno.h>
#include <console.h>
#include <printf.h>
#include <string.h>
#include <mm.h>
#include <rtc.h>

#define WRITE_INPUT_MAX     (16u * 1024u)   /* interactive 'write' buffer */
#define HEXDUMP_COLS        16

static int fail(const char *cmd, const char *arg, int rc)
{
    console_set_attr(ATTR_ERROR);
    if (rc == E_NODEV)
        kprintf("%s: not available - no P5FS file system mounted "
                "(no IDE disk with a P5FS partition was found)\n", cmd);
    else if (arg)
        kprintf("%s: %s: %s\n", cmd, arg, strerror(rc));
    else
        kprintf("%s: %s\n", cmd, strerror(rc));
    console_set_attr(ATTR_NORMAL);
    return 1;
}

static void fmt_time(u32 t, char *buf, u32 size)
{
    struct rtc_time tm;
    if (!t) {
        strlcpy(buf, "                ", size);
        return;
    }
    unix_to_rtc(t, &tm);
    snprintf(buf, size, "%04u-%02u-%02u %02u:%02u", tm.year, tm.month, tm.day,
             tm.hour, tm.minute);
}

struct ls_ctx {
    u32 files, dirs, bytes;
};

static bool ls_cb(const struct fs_entry *e, void *v)
{
    struct ls_ctx *c = v;
    char when[20];
    if (!strcmp(e->name, ".") || !strcmp(e->name, ".."))
        return true;
    fmt_time(e->mtime, when, sizeof(when));
    if (e->type == P5FS_T_DIR) {
        c->dirs++;
        console_set_attr(ATTR(COLOR_LCYAN, COLOR_BLACK));
        kprintf("  %-8s  %s  %s/\n", "<DIR>", when, e->name);
        console_set_attr(ATTR_NORMAL);
    } else {
        c->files++;
        c->bytes += e->size;
        kprintf("  %8u  %s  %s\n", e->size, when, e->name);
    }
    return true;
}

static int cmd_ls(int argc, char **argv, const char *rest)
{
    const char *path = argc > 1 ? argv[1] : ".";
    struct fs_entry st;
    int rc = fs_stat(path, &st);
    if (rc)
        return fail("ls", path, rc);
    struct ls_ctx c = { 0, 0, 0 };
    if (st.type != P5FS_T_DIR) {
        ls_cb(&st, &c);
        return 0;
    }
    rc = fs_list(path, ls_cb, &c);
    if (rc)
        return fail("ls", path, rc);
    kprintf("  %u file(s), %u bytes; %u dir(s)\n", c.files, c.bytes, c.dirs);
    return 0;
}

static int cmd_cat(int argc, char **argv, const char *rest)
{
    if (argc < 2) {
        kprintf("usage: cat <file>\n");
        return 1;
    }
    void *data;
    u32 len;
    int rc = fs_read_file(argv[1], &data, &len);
    if (rc)
        return fail("cat", argv[1], rc);
    char *s = data;
    for (u32 i = 0; i < len; i++) {
        char ch = s[i];
        if ((ch < ' ' && ch != '\n' && ch != '\t') || ch == 0x7F)
            ch = '.';                   /* keep binary data from messing up the screen */
        console_putc(ch);
    }
    if (len && s[len - 1] != '\n')
        console_putc('\n');
    kfree(data);
    return 0;
}

/* write <file> <text...>   or   write <file>  (then type lines, end with '.') */
static int do_write(int argc, char **argv, const char *rest, bool append)
{
    const char *cmd = append ? "append" : "write";
    if (argc < 2) {
        kprintf("usage: %s <file> [text]   (no text: enter lines, finish with a single '.')\n", cmd);
        return 1;
    }
    if (!fs_is_mounted())
        return fail(cmd, NULL, E_NODEV);
    /* text = raw remainder after the file name (keeps spacing) */
    const char *text = rest;
    if (*text == '"') {                 /* quoted file name */
        text = strchr(text + 1, '"');
        text = text ? text + 1 : "";
    } else {
        text += strlen(argv[1]);
    }
    while (*text == ' ')
        text++;

    int rc;
    if (*text) {
        u32 n = strlen(text);
        char *buf = kmalloc(n + 2);
        if (!buf)
            return fail(cmd, argv[1], E_NOMEM);
        memcpy(buf, text, n);
        buf[n++] = '\n';
        rc = fs_write_file(argv[1], buf, n, append);
        kfree(buf);
    } else {
        char *buf = kmalloc(WRITE_INPUT_MAX);
        char line[SHELL_LINE_MAX];
        u32 len = 0;
        if (!buf)
            return fail(cmd, argv[1], E_NOMEM);
        kprintf("Enter text. A line with a single '.' saves, Ctrl+C cancels.\n");
        for (;;) {
            int n = shell_read_line("| ", line, sizeof(line), false);
            if (n < 0) {
                kfree(buf);
                kprintf("%s: cancelled, nothing written\n", cmd);
                return 1;
            }
            if (!strcmp(line, "."))
                break;
            if (len + (u32)n + 1 > WRITE_INPUT_MAX) {
                kprintf("%s: input limit (%u bytes) reached\n", cmd, WRITE_INPUT_MAX);
                break;
            }
            memcpy(buf + len, line, (u32)n);
            len += (u32)n;
            buf[len++] = '\n';
        }
        rc = fs_write_file(argv[1], buf, len, append);
        kfree(buf);
    }
    if (rc)
        return fail(cmd, argv[1], rc);
    struct fs_entry e;
    if (fs_stat(argv[1], &e) == E_OK)
        kprintf("%s: %u bytes\n", argv[1], e.size);
    return 0;
}

static int cmd_write(int argc, char **argv, const char *rest) { return do_write(argc, argv, rest, false); }
static int cmd_append(int argc, char **argv, const char *rest) { return do_write(argc, argv, rest, true); }

static int cmd_mkdir(int argc, char **argv, const char *rest)
{
    if (argc < 2) {
        kprintf("usage: mkdir <dir>\n");
        return 1;
    }
    int rc = fs_mkdir(argv[1]);
    return rc ? fail("mkdir", argv[1], rc) : 0;
}

static int cmd_rm(int argc, char **argv, const char *rest)
{
    if (argc < 2) {
        kprintf("usage: %s <path>  (directories must be empty)\n", argv[0]);
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        int rc = fs_remove(argv[i]);
        if (rc)
            status = fail(argv[0], argv[i], rc);
    }
    return status;
}

static int cmd_cd(int argc, char **argv, const char *rest)
{
    int rc = fs_chdir(argc > 1 ? argv[1] : "/");
    return rc ? fail("cd", argc > 1 ? argv[1] : "/", rc) : 0;
}

static int cmd_pwd(int argc, char **argv, const char *rest)
{
    const char *cwd = fs_cwd();
    if (!cwd)
        return fail("pwd", NULL, E_NODEV);
    kprintf("%s\n", cwd);
    return 0;
}

static int cmd_df(int argc, char **argv, const char *rest)
{
    struct fs_info fi;
    int rc = fs_info(&fi);
    if (rc)
        return fail("df", NULL, rc);
    kprintf("Device %s, P5FS at LBA %u, label \"%s\"\n", fi.device, fi.part_start, fi.label);
    kprintf("  blocks: %u total, %u data, %u used, %u free (%u KB free)\n",
            fi.total_blocks, fi.data_blocks, fi.data_blocks - fi.free_blocks,
            fi.free_blocks, fi.free_blocks / 2);
    kprintf("  inodes: %u used of %u;  max file size %u KB, max name %u chars\n",
            fi.inodes_used, fi.inode_count, P5FS_MAX_FILE_SIZE / 1024, P5FS_NAME_MAX);
    return 0;
}

static int cmd_mount(int argc, char **argv, const char *rest)
{
    int status = 0;
    if (argc > 1) {
        int rc = fs_mount(argv[1]);
        if (rc) {
            console_set_attr(ATTR_ERROR);
            if (!ata_find(argv[1]))
                kprintf("mount: %s: no such disk (see 'disks')\n", argv[1]);
            else if (rc == E_NODEV)
                kprintf("mount: %s: no P5FS file system on this disk\n", argv[1]);
            else
                kprintf("mount: %s: %s\n", argv[1], strerror(rc));
            console_set_attr(ATTR_NORMAL);
            status = 1;
        }
    }
    struct fs_info fi;
    if (fs_info(&fi))
        kprintf("No file system mounted.\n");
    else
        kprintf("P5FS \"%s\" on %s (LBA %u) mounted at /\n", fi.label, fi.device, fi.part_start);
    return status;
}

static int cmd_fsck(int argc, char **argv, const char *rest)
{
    int rc = fs_check(true);
    if (rc < 0)
        return fail("fsck", NULL, rc);
    return rc ? 1 : 0;
}

static int cmd_disks(int argc, char **argv, const char *rest)
{
    u32 n = 0;
    for (u32 i = 0; i < ATA_MAX_DEVICES; i++) {
        struct ata_device *d = ata_get(i);
        if (!d)
            continue;
        n++;
        kprintf("%s: %s %s, \"%s\"\n", d->name, d->channel ? "secondary" : "primary",
                d->drive ? "slave" : "master", d->model);
        kprintf("     %u sectors (%u MB), %s, CHS %u/%u/%u, flush %s\n", d->sectors,
                d->sectors / 2048, d->lba ? "LBA28" : "CHS", d->cylinders, d->heads,
                d->spt, d->flush_supported ? "yes" : "no");
        kprintf("     I/O: %u sectors read, %u written, %u errors\n", d->reads, d->writes,
                d->errors);
    }
    if (!n)
        kprintf("No ATA hard disks found.\n");
    kprintf("File system: %s\n", fs_is_mounted() ? "P5FS mounted (see 'df')" : "none mounted");
    return 0;
}

/* sector <hdN> <lba> : hex dump of one sector (read only) */
static int cmd_sector(int argc, char **argv, const char *rest)
{
    u32 lba;
    if (argc < 3 || !parse_u32(argv[2], &lba)) {
        kprintf("usage: sector <hd0..hd3> <lba>\n");
        return 1;
    }
    struct ata_device *d = ata_find(argv[1]);
    if (!d) {
        kprintf("sector: no such disk '%s' (see 'disks')\n", argv[1]);
        return 1;
    }
    u8 *buf = kmalloc(ATA_SECTOR_SIZE);
    if (!buf)
        return fail("sector", NULL, E_NOMEM);
    int rc = ata_read(d, lba, 1, buf);
    if (rc) {
        kfree(buf);
        return fail("sector", argv[2], rc);
    }
    for (u32 off = 0; off < ATA_SECTOR_SIZE; off += HEXDUMP_COLS) {
        kprintf("%03x:", off);
        for (u32 i = 0; i < HEXDUMP_COLS; i++)
            kprintf(" %02x", buf[off + i]);
        kprintf("  ");
        for (u32 i = 0; i < HEXDUMP_COLS; i++) {
            u8 c = buf[off + i];
            console_putc(c >= ' ' && c < 0x7F ? (char)c : '.');
        }
        console_putc('\n');
    }
    kfree(buf);
    return 0;
}

static int cmd_date(int argc, char **argv, const char *rest)
{
    struct rtc_time t;
    if (!rtc_read(&t)) {
        kprintf("date: CMOS clock not readable or invalid\n");
        return 1;
    }
    kprintf("%04u-%02u-%02u %02u:%02u:%02u (CMOS RTC, assumed UTC)\n", t.year, t.month,
            t.day, t.hour, t.minute, t.second);
    return 0;
}

/* Write, read back and verify files of awkward sizes (block boundaries,
 * first indirect block, maximum size, oversize), then remove them and make
 * sure no blocks leaked and fsck is clean. */
static int cmd_disktest(int argc, char **argv, const char *rest)
{
    static const u32 sizes[] = { 0, 1, 511, 512, 513, 6144, 6145, 20000,
                                 P5FS_MAX_FILE_SIZE };
    struct fs_info before, after;
    int rc = fs_info(&before);
    if (rc)
        return fail("disktest", NULL, rc);
    u8 *buf = kmalloc(P5FS_MAX_FILE_SIZE + 1);
    if (!buf)
        return fail("disktest", NULL, E_NOMEM);
    bool ok = true;
    const char *name = "/~disktest.tmp";
    for (u32 i = 0; i < ARRAY_SIZE(sizes) && ok; i++) {
        u32 n = sizes[i];
        for (u32 j = 0; j < n; j++)
            buf[j] = (u8)(j * 7 + i + (j >> 9));
        rc = fs_write_file(name, buf, n, false);
        void *back = NULL;
        u32 len = 0;
        if (!rc)
            rc = fs_read_file(name, &back, &len);
        bool same = !rc && len == n && !memcmp(back, buf, n);
        kfree(back);
        kprintf("  %6u bytes: write+read %s\n", n, rc ? strerror(rc) : same ? "OK" : "MISMATCH");
        ok = !rc && same;
    }
    if (ok) {                           /* one byte over the limit must fail */
        rc = fs_write_file(name, buf, P5FS_MAX_FILE_SIZE + 1, false);
        kprintf("  %6u bytes: %s (expected 'file too large')\n", P5FS_MAX_FILE_SIZE + 1,
                rc ? strerror(rc) : "accepted?!");
        ok = rc == E_FBIG;
    }
    kfree(buf);
    rc = fs_remove(name);
    if (rc && rc != E_NOENT)
        ok = false;
    fs_info(&after);
    kprintf("  free blocks before %u, after %u: %s\n", before.free_blocks, after.free_blocks,
            before.free_blocks == after.free_blocks ? "no leak" : "LEAK");
    ok &= before.free_blocks == after.free_blocks;
    ok &= fs_check(true) == 0;
    kprintf("disktest: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

const struct shell_cmd fs_commands[] = {
    { "ls",     "[path]",        "list files", cmd_ls },
    { "cat",    "<file>",        "show a file", cmd_cat },
    { "write",  "<file> [text]", "create/overwrite a file", cmd_write },
    { "append", "<file> [text]", "append to a file", cmd_append },
    { "mkdir",  "<dir>",         "create a directory", cmd_mkdir },
    { "rm",     "<path...>",     "delete files / empty dirs", cmd_rm },
    { "rmdir",  "<dir>",         "delete an empty directory", cmd_rm },
    { "cd",     "[dir]",         "change directory", cmd_cd },
    { "pwd",    "",              "print current directory", cmd_pwd },
    { "df",     "",              "file system usage", cmd_df },
    { "mount",  "[hdN]",         "show/switch the mounted disk", cmd_mount },
    { "fsck",   "",              "check file system consistency", cmd_fsck },
    { "disks",  "",              "list IDE/ATA disks", cmd_disks },
    { "sector", "<hdN> <lba>",   "hex dump a disk sector", cmd_sector },
    { "date",   "",              "show the CMOS clock", cmd_date },
    { "disktest", "",            "file write/read/verify self test", cmd_disktest },
    { NULL, NULL, NULL, NULL },
};
