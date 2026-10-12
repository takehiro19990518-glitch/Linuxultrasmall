#ifndef P5_SHELL_H
#define P5_SHELL_H
#include <types.h>

#define SHELL_LINE_MAX  160
#define SHELL_ARGS_MAX  16

/* argv[0] is the command name; 'rest' is the raw text after the name. */
typedef int (*shell_fn)(int argc, char **argv, const char *rest);

struct shell_cmd {
    const char *name;
    const char *usage;
    const char *help;
    shell_fn fn;
};

void shell_run(void) NORETURN;
/* Read one line with editing and history; returns length or -1 on Ctrl+C. */
int shell_read_line(const char *prompt, char *buf, u32 size, bool use_history);

/* command tables (kernel/shell/cmd_*.c) */
extern const struct shell_cmd sys_commands[];
extern const struct shell_cmd mem_commands[];
extern const struct shell_cmd fs_commands[];
#endif
