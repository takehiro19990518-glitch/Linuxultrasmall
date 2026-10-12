################################################################################
# libcroco: CSS parsing library (needed by the C version of librsvg)
################################################################################
LIBCROCO3_VERSION = 0.6.13
LIBCROCO3_SOURCE = libcroco_$(LIBCROCO3_VERSION).orig.tar.xz
LIBCROCO3_SITE = http://archive.ubuntu.com/ubuntu/pool/main/libc/libcroco
LIBCROCO3_LICENSE = LGPL-2.0
LIBCROCO3_LICENSE_FILES = COPYING.LIB
LIBCROCO3_INSTALL_STAGING = YES
LIBCROCO3_DEPENDENCIES = host-pkgconf libglib2 libxml2
LIBCROCO3_CONF_OPTS = --disable-gtk-doc --disable-checks
$(eval $(autotools-package))
