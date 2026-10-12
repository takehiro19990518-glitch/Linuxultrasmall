################################################################################
# xbill: classic X11 arcade game (Athena widgets)
################################################################################
XBILL_VERSION = 2.1
XBILL_SOURCE = xbill_$(XBILL_VERSION).orig.tar.gz
XBILL_SITE = http://archive.ubuntu.com/ubuntu/pool/universe/x/xbill
XBILL_LICENSE = GPL-2.0+
XBILL_LICENSE_FILES = README
XBILL_DEPENDENCIES = xlib_libXaw xlib_libXpm xlib_libXt
XBILL_CONF_OPTS = --disable-gtk --disable-motif --enable-athena
XBILL_CONF_ENV = CFLAGS="$(TARGET_CFLAGS) -fcommon" LIBS="-lXpm"
$(eval $(autotools-package))
