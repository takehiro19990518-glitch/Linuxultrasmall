/* Placeholders until storage (Phase 4) exists: the commands are listed
 * but clearly report that they are unavailable. */
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

const struct shell_cmd fs_commands[] = {
    { "ls",    "[dir]",          "list files (unavailable)", unavailable },
    { "cat",   "<file>",         "show a file (unavailable)", unavailable },
    { "mkdir", "<dir>",          "create a directory (unavailable)", unavailable },
    { "write", "<file> [text]",  "create/write a file (unavailable)", unavailable },
    { "rm",    "<path>",         "delete a file (unavailable)", unavailable },
    { NULL, NULL, NULL, NULL },
};
