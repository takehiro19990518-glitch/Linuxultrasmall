/*
 * p3-oomguard: early out-of-memory guard for low-memory PCs (64MB).
 *
 * When RAM and (zram) swap are both almost used up, Linux can spend minutes
 * thrashing (re-reading program code from the CD) before its own OOM killer
 * acts, and the PC looks frozen.  This daemon checks /proc/meminfo twice a
 * second and, when both are nearly exhausted, kills the application with
 * the highest oom_score - never the X server, the window manager, the
 * desktop panel or system daemons.
 *
 * It is linked statically and locks itself in memory so it keeps running
 * while the rest of the system is paging.
 */
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static const char *protected_names[] = {
    "init", "Xorg", "fluxbox", "p3-start", "p3-oomguard", "p3-session",
    "p3-startx", "sh", "ash", "getty", "login", "syslogd", "klogd",
    "udevd", "dbus-daemon", "udhcpc", "wpa_supplicant", NULL
};

static long meminfo(const char *buf, const char *key) {
    const char *p = strstr(buf, key);
    return p ? atol(p + strlen(key)) : -1;
}

static int read_file(const char *path, char *buf, size_t len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = 0;
    return (int)n;
}

static void logmsg(const char *msg) {
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd >= 0) { (void)write(fd, msg, strlen(msg)); close(fd); }
}

/* returns the pid of the best victim, or -1 */
static int pick_victim(char *name, size_t nlen) {
    DIR *d = opendir("/proc");
    struct dirent *e;
    int best = -1, self = getpid();
    long best_score = -1;
    char path[64], buf[256];
    if (!d) return -1;
    while ((e = readdir(d))) {
        int pid = atoi(e->d_name), i, prot = 0;
        if (pid <= 1 || pid == self) continue;
        snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
        if (read_file(path, buf, sizeof buf) <= 0) continue;      /* kernel thread */
        snprintf(path, sizeof path, "/proc/%d/comm", pid);
        if (read_file(path, buf, sizeof buf) <= 0) continue;
        buf[strcspn(buf, "\n")] = 0;
        for (i = 0; protected_names[i]; i++)
            if (!strcmp(buf, protected_names[i])) prot = 1;
        if (prot) continue;
        char comm[32];
        snprintf(comm, sizeof comm, "%s", buf);
        snprintf(path, sizeof path, "/proc/%d/oom_score", pid);
        if (read_file(path, buf, sizeof buf) <= 0) continue;
        long score = atol(buf);
        if (score > best_score) {
            best_score = score;
            best = pid;
            snprintf(name, nlen, "%s", comm);
        }
    }
    closedir(d);
    return best;
}

int main(void) {
    char buf[2048], name[32], msg[160];
    struct timespec half = {0, 500000000};

    mlockall(MCL_CURRENT | MCL_FUTURE);
    setpriority(PRIO_PROCESS, 0, -15);
    for (;;) {
        nanosleep(&half, NULL);
        if (read_file("/proc/meminfo", buf, sizeof buf) <= 0) continue;
        long total = meminfo(buf, "MemTotal:"), avail = meminfo(buf, "MemAvailable:");
        long stotal = meminfo(buf, "SwapTotal:"), sfree = meminfo(buf, "SwapFree:");
        long mem_min = total * 6 / 100;          /* kB */
        if (mem_min < 4096) mem_min = 4096;
        if (avail < 0 || avail >= mem_min) continue;
        if (stotal > 0 && sfree * 10 >= stotal) continue;
        int pid = pick_victim(name, sizeof name);
        if (pid <= 0) continue;
        kill(pid, SIGKILL);
        snprintf(msg, sizeof msg,
                 "p3-oomguard: out of memory, closed %s (pid %d) to keep the PC responsive\n",
                 name, pid);
        logmsg(msg);
        sleep(2);
    }
}
