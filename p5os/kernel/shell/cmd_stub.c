/* Placeholders until memory management (Phase 3) and storage (Phase 4)
 * exist: the commands are listed but clearly report they are unavailable. */
#include <shell.h>
#include <console.h>
#include <printf.h>

static int unavailable(int argc, char **argv, const char *rest)
{
    console_set_attr(ATTR_WARN);
    kprintf("%s: not available - storage/file system support is not implemented yet\n", argv[0]);
    console_set_attr(ATTR_NORMAL);
    return 1;
}

static int mem_unavailable(int argc, char **argv, const char *rest)
{
    console_set_attr(ATTR_WARN);
    kprintf("mem: not available - memory manager is not implemented yet\n");
    console_set_attr(ATTR_NORMAL);
    return 1;
}

const struct shell_cmd mem_commands[] = {
    { "mem", "", "memory usage (unavailable)", mem_unavailable },
    { NULL, NULL, NULL, NULL },
};

const struct shell_cmd fs_commands[] = {
    { "ls",    "[dir]",          "list files (unavailable)", unavailable },
    { "cat",   "<file>",         "show a file (unavailable)", unavailable },
    { "mkdir", "<dir>",          "create a directory (unavailable)", unavailable },
    { "write", "<file> [text]",  "create/write a file (unavailable)", unavailable },
    { "rm",    "<path>",         "delete a file (unavailable)", unavailable },
    { NULL, NULL, NULL, NULL },
};
