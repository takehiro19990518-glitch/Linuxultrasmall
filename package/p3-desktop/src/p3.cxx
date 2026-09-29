// P3 Linux desktop tools (FLTK 1.3), one multi-call binary:
//   p3-start     Start button + application menu (bottom-left of the screen)
//   p3-files     simple file manager
//   p3-edit      simple text editor
//   p3-sysinfo   system information / welcome window
//   p3-shutdown  shut down / reboot dialog
// Written for low-end hardware: no threads, no images, only FLTK + Xlib.
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_File_Browser.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/filename.H>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const Fl_Color P3_BLUE = fl_rgb_color(0, 64, 128);
static const Fl_Color P3_TEAL = fl_rgb_color(0, 128, 128);

// ------------------------------------------------------------ helpers ----
static void spawn(const std::string &cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() == 0) {
            execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)0);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) waitpid(pid, 0, 0);
}

static std::string shq(const std::string &s) {  // shell quote
    std::string r = "'";
    for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
    return r + "'";
}

static std::string read_file(const char *path) {
    std::string out;
    FILE *f = fopen(path, "r");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

static std::string field(const std::string &text, const char *key) {
    size_t p = text.find(key);
    if (p == std::string::npos) return "";
    p = text.find(':', p);
    if (p == std::string::npos) return "";
    size_t e = text.find('\n', p);
    std::string v = text.substr(p + 1, e - p - 1);
    while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
    return v;
}

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

static void p3_theme() {
    Fl::scheme("gtk+");
    Fl::background(212, 208, 200);
    Fl::set_font(FL_HELVETICA, "DejaVu Sans");
    Fl::set_font(FL_HELVETICA_BOLD, "DejaVu Sans Bold");
    Fl::set_font(FL_COURIER, "DejaVu Sans Mono");
    FL_NORMAL_SIZE = 12;
}

// ------------------------------------------------------------ p3-start ---
static void start_cb(Fl_Widget *, void *v) { spawn((const char *)v); }

static int app_start(int argc, char **argv) {
    int sh = Fl::h();
    Fl_Window *w = new Fl_Window(0, sh - 24, 64, 24, "p3-start");
    Fl_Menu_Button *b = new Fl_Menu_Button(0, 0, 64, 24, "@-2> Start");
    b->box(FL_UP_BOX);
    b->color(P3_TEAL);
    b->labelcolor(FL_WHITE);
    b->labelfont(FL_HELVETICA_BOLD);
    b->labelsize(12);
    b->add("Terminal", 0, start_cb, (void *)"xterm");
    b->add("File Manager", 0, start_cb, (void *)"p3-files");
    b->add("Text Editor", 0, start_cb, (void *)"p3-edit");
    b->add("System Information", 0, start_cb, (void *)"p3-sysinfo", FL_MENU_DIVIDER);
    b->add("Settings/Screen 640x480", 0, start_cb, (void *)"p3-setres 640x480");
    b->add("Settings/Screen 800x600", 0, start_cb, (void *)"p3-setres 800x600");
    b->add("Settings/Screen 1024x768", 0, start_cb, (void *)"p3-setres 1024x768");
    b->add("Settings/Network (DHCP)", 0, start_cb,
           (void *)"xterm -hold -e sh -c 'udhcpc -i eth0 -n -q; ifconfig eth0'", FL_MENU_DIVIDER);
    b->add("Install to Hard Disk...", 0, start_cb, (void *)"xterm -e p3-install", FL_MENU_DIVIDER);
    b->add("Shut Down...", 0, start_cb, (void *)"p3-shutdown");
    w->end();
    w->border(0);
    w->xclass("p3-start");
    w->show(1, argv);
    (void)argc;
    return Fl::run();
}

// ------------------------------------------------------------ p3-shutdown
static int app_shutdown(int, char **) {
    int r = fl_choice("What do you want the computer to do?", "Cancel", "Restart", "Shut Down");
    if (r == 1) { sync(); execl("/sbin/reboot", "reboot", (char *)0); }
    if (r == 2) { sync(); execl("/sbin/poweroff", "poweroff", (char *)0); }
    return 0;
}

// ------------------------------------------------------------ p3-sysinfo -
class Logo : public Fl_Box {
public:
    Logo(int x, int y, int w, int h) : Fl_Box(x, y, w, h) {}
    void draw() override {
        fl_color(P3_BLUE);
        fl_rectf(x(), y(), w(), h());
        // three stripes: the "P3" mark
        const Fl_Color c[3] = {fl_rgb_color(220, 60, 40), fl_rgb_color(250, 190, 30), fl_rgb_color(60, 170, 70)};
        for (int i = 0; i < 3; i++) { fl_color(c[i]); fl_rectf(x() + 12 + i * 10, y() + 12, 8, h() - 24); }
        fl_color(FL_WHITE);
        fl_font(FL_HELVETICA_BOLD, 26);
        fl_draw("P3 Linux", x() + 56, y() + 38);
        fl_font(FL_HELVETICA, 12);
        fl_draw("Linux for Pentium III", x() + 58, y() + 56);
    }
};

static std::string ip_of(const char *ifn) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return "";
    struct ifreq r;
    memset(&r, 0, sizeof r);
    strncpy(r.ifr_name, ifn, IFNAMSIZ - 1);
    std::string out;
    if (ioctl(s, SIOCGIFADDR, &r) == 0)
        out = inet_ntoa(((struct sockaddr_in *)&r.ifr_addr)->sin_addr);
    close(s);
    return out;
}

static std::string sysinfo_text() {
    std::string cpu = read_file("/proc/cpuinfo");
    std::string mem = read_file("/proc/meminfo");
    std::string rel = trim(read_file("/etc/p3linux-release"));
    struct utsname u;
    uname(&u);
    struct sysinfo si;
    sysinfo(&si);
    long totalmb = 0, availmb = 0;
    sscanf(field(mem, "MemTotal").c_str(), "%ld", &totalmb);
    sscanf(field(mem, "MemAvailable").c_str(), "%ld", &availmb);
    std::string flags = " " + field(cpu, "flags") + " ";
    auto has = [&](const char *f) { return flags.find(std::string(" ") + f + " ") != std::string::npos; };
    int ncpu = 0;
    for (size_t p = 0; (p = cpu.find("processor", p)) != std::string::npos; p++) ncpu++;
    char buf[2048];
    std::string ip = ip_of("eth0");
    snprintf(buf, sizeof buf,
             "Version:       %s\n"
             "CPU:           %s\n"
             "CPU clock:     %s MHz  (%d CPU%s)\n"
             "CPU features:  MMX %s   SSE %s   SSE2 %s\n"
             "Memory:        %ld MB total, %ld MB available\n"
             "Kernel:        %s %s\n"
             "Architecture:  %s (32-bit, built for i686/pentium3)\n"
             "Screen:        %d x %d\n"
             "Network eth0:  %s\n"
             "Uptime:        %ld min\n",
             rel.empty() ? "P3 Linux" : rel.c_str(),
             field(cpu, "model name").c_str(), field(cpu, "cpu MHz").c_str(), ncpu, ncpu > 1 ? "s" : "",
             has("mmx") ? "yes" : "no", has("sse") ? "yes" : "no", has("sse2") ? "yes" : "no (not needed)",
             totalmb / 1024, availmb / 1024, u.sysname, u.release, u.machine, Fl::w(), Fl::h(),
             ip.empty() ? "not configured" : ip.c_str(), si.uptime / 60);
    return buf;
}

static Fl_Box *g_info;
static void refresh_cb(Fl_Widget *, void *) { g_info->copy_label(sysinfo_text().c_str()); }

static int app_sysinfo(int argc, char **argv) {
    Fl_Double_Window *w = new Fl_Double_Window(460, 330, "System Information - P3 Linux");
    new Logo(0, 0, 460, 70);
    g_info = new Fl_Box(10, 78, 440, 200);
    g_info->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_TOP);
    g_info->labelfont(FL_COURIER);
    g_info->labelsize(12);
    g_info->box(FL_DOWN_BOX);
    g_info->color(FL_WHITE);
    refresh_cb(0, 0);
    Fl_Button *r = new Fl_Button(250, 292, 95, 26, "Refresh");
    r->callback(refresh_cb);
    Fl_Button *c = new Fl_Button(355, 292, 95, 26, "Close");
    c->callback([](Fl_Widget *, void *wv) { ((Fl_Window *)wv)->hide(); }, w);
    w->end();
    w->show(argc > 1 ? 1 : argc, argv);
    return Fl::run();
}

// ------------------------------------------------------------ p3-edit ----
struct Editor {
    Fl_Double_Window *win;
    Fl_Text_Buffer *buf;
    Fl_Text_Editor *ed;
    std::string path;
    bool changed = false;
    void title() {
        std::string t = (path.empty() ? "Untitled" : fl_filename_name(path.c_str()));
        t += changed ? " *" : "";
        t += " - P3 Edit";
        win->copy_label(t.c_str());
    }
    bool confirm() {
        if (!changed) return true;
        int r = fl_choice("The file has unsaved changes.", "Cancel", "Discard", "Save");
        if (r == 2) return save(false);
        return r == 1;
    }
    void load(const std::string &p) {
        if (buf->loadfile(p.c_str()) != 0 && errno != ENOENT) fl_alert("Cannot open %s:\n%s", p.c_str(), strerror(errno));
        path = p;
        changed = false;
        title();
    }
    bool save(bool as) {
        if (as || path.empty()) {
            const char *f = fl_file_chooser("Save As", "*", path.c_str());
            if (!f) return false;
            path = f;
        }
        if (buf->savefile(path.c_str()) != 0) { fl_alert("Cannot save %s:\n%s", path.c_str(), strerror(errno)); return false; }
        changed = false;
        title();
        return true;
    }
};
static Editor E;

static void ed_new(Fl_Widget *, void *) { if (E.confirm()) { E.buf->text(""); E.path.clear(); E.changed = false; E.title(); } }
static void ed_open(Fl_Widget *, void *) {
    if (!E.confirm()) return;
    const char *f = fl_file_chooser("Open File", "*", E.path.c_str());
    if (f) E.load(f);
}
static void ed_save(Fl_Widget *, void *) { E.save(false); }
static void ed_saveas(Fl_Widget *, void *) { E.save(true); }
static void ed_quit(Fl_Widget *, void *) { if (E.confirm()) E.win->hide(); }
static void ed_cut(Fl_Widget *, void *) { Fl_Text_Editor::kf_cut(0, E.ed); }
static void ed_copy(Fl_Widget *, void *) { Fl_Text_Editor::kf_copy(0, E.ed); }
static void ed_paste(Fl_Widget *, void *) { Fl_Text_Editor::kf_paste(0, E.ed); }
static void ed_all(Fl_Widget *, void *) { Fl_Text_Editor::kf_select_all(0, E.ed); }
static void ed_modified(int, int ins, int del, int, const char *, void *) {
    if ((ins || del) && !E.changed) { E.changed = true; E.title(); }
}

static int app_edit(int argc, char **argv) {
    E.win = new Fl_Double_Window(600, 420);
    Fl_Menu_Bar *m = new Fl_Menu_Bar(0, 0, 600, 24);
    m->add("&File/&New", FL_CTRL + 'n', ed_new);
    m->add("&File/&Open...", FL_CTRL + 'o', ed_open);
    m->add("&File/&Save", FL_CTRL + 's', ed_save);
    m->add("&File/Save &As...", FL_CTRL + FL_SHIFT + 's', ed_saveas, 0, FL_MENU_DIVIDER);
    m->add("&File/&Quit", FL_CTRL + 'q', ed_quit);
    m->add("&Edit/Cu&t", FL_CTRL + 'x', ed_cut);
    m->add("&Edit/&Copy", FL_CTRL + 'c', ed_copy);
    m->add("&Edit/&Paste", FL_CTRL + 'v', ed_paste);
    m->add("&Edit/Select &All", FL_CTRL + 'a', ed_all);
    E.buf = new Fl_Text_Buffer();
    E.ed = new Fl_Text_Editor(0, 24, 600, 396);
    E.ed->buffer(E.buf);
    E.ed->textfont(FL_COURIER);
    E.ed->textsize(13);
    E.buf->add_modify_callback(ed_modified, 0);
    E.win->resizable(E.ed);
    E.win->callback(ed_quit);
    E.win->end();
    if (argc > 1) E.load(argv[1]); else E.title();
    E.win->show();
    return Fl::run();
}

// ------------------------------------------------------------ p3-files ---
struct Files {
    Fl_Double_Window *win;
    Fl_Input *loc;
    Fl_File_Browser *list;
    Fl_Box *status;
    std::string cwd;
    std::string clip;
    bool clip_cut = false;
};
static Files F;

static std::string join(const std::string &d, const std::string &n) { return d == "/" ? "/" + n : d + "/" + n; }

static void fm_go(const std::string &dir) {
    char real[4096];
    if (!realpath(dir.c_str(), real)) { fl_alert("Cannot open %s", dir.c_str()); return; }
    struct stat st;
    if (stat(real, &st) || !S_ISDIR(st.st_mode)) { fl_alert("%s is not a folder", real); return; }
    F.cwd = real;
    F.loc->value(real);
    F.list->load(real, fl_casenumericsort);
    // drop "../" entry at "/"
    if (F.cwd == "/" && F.list->size() && !strcmp(F.list->text(1), "../")) F.list->remove(1);
    char s[128];
    snprintf(s, sizeof s, "%d items", F.list->size());
    F.status->copy_label(s);
    std::string t = F.cwd + " - P3 Files";
    F.win->copy_label(t.c_str());
    chdir(real);
}

static std::string fm_selected() {
    int i = F.list->value();
    if (i <= 0) return "";
    std::string n = F.list->text(i);
    if (!n.empty() && n.back() == '/') n.pop_back();
    return n;
}

static void fm_open(Fl_Widget *, void *) {
    std::string n = fm_selected();
    if (n.empty()) return;
    std::string p = n == ".." ? F.cwd + "/.." : join(F.cwd, n);
    if (fl_filename_isdir(p.c_str())) fm_go(p);
    else spawn("p3-edit " + shq(p));
}
static void fm_list_cb(Fl_Widget *, void *) { if (Fl::event_clicks()) { Fl::event_clicks(0); fm_open(0, 0); } }
static void fm_up(Fl_Widget *, void *) { fm_go(F.cwd + "/.."); }
static void fm_home(Fl_Widget *, void *) { const char *h = getenv("HOME"); fm_go(h ? h : "/"); }
static void fm_loc(Fl_Widget *, void *) { fm_go(F.loc->value()); }
static void fm_refresh(Fl_Widget *, void *) { fm_go(F.cwd); }
static void fm_term(Fl_Widget *, void *) { spawn("cd " + shq(F.cwd) + " && xterm"); }
static void fm_mkdir(Fl_Widget *, void *) {
    const char *n = fl_input("New folder name:", "New Folder");
    if (n && *n && mkdir(join(F.cwd, n).c_str(), 0755)) fl_alert("mkdir failed: %s", strerror(errno));
    fm_refresh(0, 0);
}
static void fm_newfile(Fl_Widget *, void *) {
    const char *n = fl_input("New file name:", "new.txt");
    if (n && *n) {
        int fd = open(join(F.cwd, n).c_str(), O_CREAT | O_WRONLY | O_EXCL, 0644);
        if (fd < 0) fl_alert("Cannot create file: %s", strerror(errno)); else close(fd);
    }
    fm_refresh(0, 0);
}
static void fm_rename(Fl_Widget *, void *) {
    std::string n = fm_selected();
    if (n.empty() || n == "..") return;
    const char *nn = fl_input("Rename to:", n.c_str());
    if (nn && *nn && rename(join(F.cwd, n).c_str(), join(F.cwd, nn).c_str())) fl_alert("Rename failed: %s", strerror(errno));
    fm_refresh(0, 0);
}
static void fm_delete(Fl_Widget *, void *) {
    std::string n = fm_selected();
    if (n.empty() || n == "..") return;
    if (fl_choice("Delete \"%s\" permanently?", "Cancel", "Delete", 0, n.c_str()) != 1) return;
    std::string cmd = "rm -rf -- " + shq(join(F.cwd, n));
    if (system(cmd.c_str())) fl_alert("Delete failed");
    fm_refresh(0, 0);
}
static void fm_copy(Fl_Widget *, void *cut) {
    std::string n = fm_selected();
    if (n.empty() || n == "..") return;
    F.clip = join(F.cwd, n);
    F.clip_cut = cut != 0;
    std::string s = std::string(F.clip_cut ? "Cut: " : "Copied: ") + n;
    F.status->copy_label(s.c_str());
}
static void fm_paste(Fl_Widget *, void *) {
    if (F.clip.empty()) return;
    std::string cmd = (F.clip_cut ? "mv -- " : "cp -a -- ") + shq(F.clip) + " " + shq(F.cwd + "/");
    if (system(cmd.c_str())) fl_alert("Paste failed");
    if (F.clip_cut) F.clip.clear();
    fm_refresh(0, 0);
}

static int app_files(int argc, char **argv) {
    const int W = 560, H = 400;
    F.win = new Fl_Double_Window(W, H);
    Fl_Menu_Bar *m = new Fl_Menu_Bar(0, 0, W, 24);
    m->add("&File/&Open", FL_Enter, fm_open);
    m->add("&File/New &Folder...", FL_CTRL + FL_SHIFT + 'n', fm_mkdir);
    m->add("&File/New F&ile...", 0, fm_newfile);
    m->add("&File/&Rename...", FL_F + 2, fm_rename);
    m->add("&File/&Delete...", FL_Delete, fm_delete, 0, FL_MENU_DIVIDER);
    m->add("&File/&Close", FL_CTRL + 'w', [](Fl_Widget *, void *) { F.win->hide(); });
    m->add("&Edit/Cu&t", FL_CTRL + 'x', fm_copy, (void *)1);
    m->add("&Edit/&Copy", FL_CTRL + 'c', fm_copy, 0);
    m->add("&Edit/&Paste", FL_CTRL + 'v', fm_paste);
    m->add("&Go/&Up", FL_ALT + FL_Up, fm_up);
    m->add("&Go/&Home", FL_ALT + FL_Home, fm_home);
    m->add("&Go/&Root (/)", 0, [](Fl_Widget *, void *) { fm_go("/"); });
    m->add("&Go/&Media (/mnt)", 0, [](Fl_Widget *, void *) { fm_go("/mnt"); });
    m->add("&Go/Re&fresh", FL_F + 5, fm_refresh);
    m->add("&Tools/Open &Terminal Here", FL_F + 4, fm_term);
    Fl_Button *up = new Fl_Button(2, 27, 50, 24, "@8->");
    up->tooltip("Up one folder");
    up->callback(fm_up);
    Fl_Button *home = new Fl_Button(54, 27, 50, 24, "Home");
    home->callback(fm_home);
    F.loc = new Fl_Input(108, 27, W - 110, 24);
    F.loc->when(FL_WHEN_ENTER_KEY);
    F.loc->callback(fm_loc);
    F.list = new Fl_File_Browser(2, 54, W - 4, H - 76);
    F.list->type(FL_HOLD_BROWSER);
    F.list->iconsize(16);
    F.list->textsize(12);
    F.list->callback(fm_list_cb);
    F.status = new Fl_Box(2, H - 21, W - 4, 20);
    F.status->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT);
    F.status->box(FL_THIN_DOWN_BOX);
    F.win->resizable(F.list);
    F.win->end();
    Fl_File_Icon::load_system_icons();
    const char *h = getenv("HOME");
    fm_go(argc > 1 ? argv[1] : (h ? h : "/"));
    F.win->show();
    return Fl::run();
}

// ------------------------------------------------------------ main -------
int main(int argc, char **argv) {
    signal(SIGCHLD, SIG_IGN);
    p3_theme();
    std::string me = fl_filename_name(argv[0]);
    if (me == "p3-desktop" && argc > 1) { me = argv[1]; argv++; argc--; }
    if (me == "p3-start") return app_start(argc, argv);
    if (me == "p3-files") return app_files(argc, argv);
    if (me == "p3-edit") return app_edit(argc, argv);
    if (me == "p3-sysinfo") return app_sysinfo(argc, argv);
    if (me == "p3-shutdown") return app_shutdown(argc, argv);
    fprintf(stderr, "usage: p3-desktop {p3-start|p3-files|p3-edit|p3-sysinfo|p3-shutdown}\n");
    return 1;
}
