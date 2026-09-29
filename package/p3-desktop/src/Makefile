FLTK_CONFIG ?= fltk-config
CXX ?= g++
PREFIX ?= /usr
APPS = p3-start p3-files p3-edit p3-sysinfo p3-shutdown

all: p3-desktop

p3-desktop: p3.cxx
	$(CXX) $(CXXFLAGS) -std=c++11 $(shell $(FLTK_CONFIG) --cxxflags) -o $@ p3.cxx \
		$(LDFLAGS) $(shell $(FLTK_CONFIG) --ldflags)

install: p3-desktop
	install -D -m 0755 p3-desktop $(DESTDIR)$(PREFIX)/bin/p3-desktop
	for a in $(APPS); do ln -sf p3-desktop $(DESTDIR)$(PREFIX)/bin/$$a; done

clean:
	rm -f p3-desktop
