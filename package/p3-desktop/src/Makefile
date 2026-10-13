FLTK_CONFIG ?= fltk-config
CXX ?= g++
CC ?= gcc
PREFIX ?= /usr
APPS = p3-start p3-files p3-edit p3-sysinfo p3-shutdown p3-openfile p3-needram p3-desk

all: p3-desktop p3-oomguard

p3-desktop: p3.cxx
	$(CXX) $(CXXFLAGS) -std=c++11 $(shell $(FLTK_CONFIG) --cxxflags) -o $@ p3.cxx \
		$(LDFLAGS) $(shell $(FLTK_CONFIG) --ldflags)

# static and tiny: it locks itself in memory
p3-oomguard: p3-oomguard.c
	$(CC) $(CFLAGS) -static -o $@ p3-oomguard.c $(LDFLAGS)

install: p3-desktop p3-oomguard
	install -D -m 0755 p3-desktop $(DESTDIR)$(PREFIX)/bin/p3-desktop
	for a in $(APPS); do ln -sf p3-desktop $(DESTDIR)$(PREFIX)/bin/$$a; done
	install -D -m 0755 p3-oomguard $(DESTDIR)$(PREFIX)/sbin/p3-oomguard

clean:
	rm -f p3-desktop p3-oomguard
