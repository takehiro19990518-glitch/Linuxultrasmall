################################################################################
# xjump: simple falling-tower jumping game for X11
################################################################################
XJUMP_VERSION = 2.9.3
XJUMP_SOURCE = xjump_$(XJUMP_VERSION).orig.tar.gz
XJUMP_SITE = http://archive.ubuntu.com/ubuntu/pool/universe/x/xjump
XJUMP_LICENSE = GPL-2.0+
XJUMP_LICENSE_FILES = COPYING
XJUMP_AUTORECONF = YES
XJUMP_DEPENDENCIES = xlib_libXaw xlib_libXpm xlib_libXt
XJUMP_CONF_ENV = CFLAGS="$(TARGET_CFLAGS) -std=gnu17"
$(eval $(autotools-package))
