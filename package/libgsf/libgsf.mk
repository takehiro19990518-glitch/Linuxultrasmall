################################################################################
# libgsf: structured file (OLE2/ZIP) I/O library
################################################################################
LIBGSF_VERSION = 1.14.53
LIBGSF_SOURCE = libgsf_$(LIBGSF_VERSION).orig.tar.xz
LIBGSF_SITE = http://archive.ubuntu.com/ubuntu/pool/main/libg/libgsf
LIBGSF_LICENSE = LGPL-2.1
LIBGSF_LICENSE_FILES = COPYING
LIBGSF_INSTALL_STAGING = YES
LIBGSF_DEPENDENCIES = host-pkgconf libglib2 libxml2 zlib
LIBGSF_CONF_OPTS = --disable-nls --disable-introspection --disable-gtk-doc \
	--without-gdk-pixbuf --without-bz2
$(eval $(autotools-package))
