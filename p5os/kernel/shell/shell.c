/* Command-line shell: line editor with history, tokenizer, dispatcher. */
#include <shell.h>
#include <console.h>
#include <keyboard.h>
#include <printf.h>
#include <string.h>
#include <system.h>
#include <klog.h>
#include <fs.h>

#define HISTORY_SIZE    16
#define CTRL_C          3
#define CTRL_L          12

static char history[HISTORY_SIZE][SHELL_LINE_MAX];
static u32 hist_count;      /* total lines ever stored */

static void history_add(const char *line)
{
    if (!*line)
        return;
    if (hist_count && !strcmp(history[(hist_count - 1) % HISTORY_SIZE], line))
        return;             /* ignore immediate duplicates */
    strlcpy(history[hist_count % HISTORY_SIZE], line, SHELL_LINE_MAX);
    hist_count++;
}

static void erase_chars(u32 n)
{
    while (n--)
        console_write("\b \b");
}

int shell_read_line(const char *prompt, char *buf, u32 size, bool use_history)
{
    u32 len = 0;
    u32 hist_pos = hist_count;  /* == hist_count means "editing new line" */
    char saved[SHELL_LINE_MAX] = "";

    console_write(prompt);
    buf[0] = '\0';
    for (;;) {
        int k = kbd_getkey();
        if (k == '\n') {
            console_putc('\n');
            buf[len] = '\0';
            return (int)len;
        }
        if (k == '\b') {
            if (len) {
                len--;
                erase_chars(1);
            }
        } else if (k == CTRL_C) {
            console_write("^C\n");
            buf[0] = '\0';
            return -1;
        } else if (k == CTRL_L) {
            console_clear();
            buf[len] = '\0';
            console_write(prompt);
            console_write(buf);
        } else if (k == KEY_CTRL_ALT_DEL) {
            console_write("\nCtrl+Alt+Del\n");
            system_reboot();
        } else if ((k == KEY_UP || k == KEY_DOWN) && use_history) {
            u32 oldest = hist_count > HISTORY_SIZE ? hist_count - HISTORY_SIZE : 0;
            u32 np = hist_pos;
            if (k == KEY_UP && hist_pos > oldest)
                np = hist_pos - 1;
            else if (k == KEY_DOWN && hist_pos < hist_count)
                np = hist_pos + 1;
            if (np == hist_pos)
                continue;
            if (hist_pos == hist_count) {
                buf[len] = '\0';
                strlcpy(saved, buf, sizeof(saved));
            }
            hist_pos = np;
            erase_chars(len);
            strlcpy(buf, hist_pos == hist_count ? saved : history[hist_pos % HISTORY_SIZE], size);
            len = strlen(buf);
            console_write(buf);
        } else if (k >= ' ' && k < 0x7F) {
            if (len + 1 < size) {
                buf[len++] = (char)k;
                console_putc((char)k);
            }
        }
        /* other keys (tab, F-keys, arrows left/right) are ignored */
    }
}

/* Split 'line' in place.  Double quotes group words: write a "b c" */
static int tokenize(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (*p == '"') {
            argv[argc++] = ++p;
            while (*p && *p != '"')
                p++;
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ')
                p++;
        }
        if (*p)
            *p++ = '\0';
    }
    return argc;
}

static const struct shell_cmd *const tables[] = { sys_commands, mem_commands, fs_commands };

static const struct shell_cmd *find_cmd(const char *name)
{
    for (u32 t = 0; t < ARRAY_SIZE(tables); t++)
        for (const struct shell_cmd *c = tables[t]; c->name; c++)
            if (!strcmp(c->name, name))
                return c;
    return NULL;
}

void shell_print_help(void);
void shell_print_help(void)
{
    for (u32 t = 0; t < ARRAY_SIZE(tables); t++)
        for (const struct shell_cmd *c = tables[t]; c->name; c++) {
            char left[32];
            snprintf(left, sizeof(left), "%s %s", c->name, c->usage);
            kprintf("  %-24s %s\n", left, c->help);
        }
}

static void execute(char *line)
{
    char raw[SHELL_LINE_MAX];
    char *argv[SHELL_ARGS_MAX + 1];

    while (*line == ' ')
        line++;
    strlcpy(raw, line, sizeof(raw));
    int argc = tokenize(line, argv, SHELL_ARGS_MAX);
    if (argc == 0)
        return;
    argv[argc] = NULL;

    /* raw text after the command name (for echo/write) */
    const char *rest = raw + strlen(argv[0]);
    while (*rest == ' ')
        rest++;

    const struct shell_cmd *c = find_cmd(argv[0]);
    if (!c) {
        console_set_attr(ATTR_ERROR);
        kprintf("%s: unknown command (type 'help')\n", argv[0]);
        console_set_attr(ATTR_NORMAL);
        return;
    }
    c->fn(argc, argv, rest);
}

void shell_run(void)
{
    char line[SHELL_LINE_MAX];
    char prompt[SHELL_LINE_MAX + 16];
    for (;;) {
        const char *cwd = fs_cwd();
        if (cwd)
            snprintf(prompt, sizeof(prompt), "P5OS:%s> ", cwd);
        else
            snprintf(prompt, sizeof(prompt), "P5OS> ");
        console_set_attr(ATTR_BRIGHT);
        int n = shell_read_line(prompt, line, sizeof(line), true);
        console_set_attr(ATTR_NORMAL);
        if (n <= 0)
            continue;
        history_add(line);
        execute(line);
    }
}
