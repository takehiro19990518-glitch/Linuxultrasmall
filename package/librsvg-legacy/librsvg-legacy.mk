################################################################################
# librsvg-legacy: librsvg 2.40 (C implementation; newer versions need Rust,
# whose i686 target assumes SSE2)
################################################################################
LIBRSVG_LEGACY_VERSION = 2.40.20
LIBRSVG_LEGACY_SOURCE = librsvg_$(LIBRSVG_LEGACY_VERSION).orig.tar.xz
LIBRSVG_LEGACY_SITE = http://archive.ubuntu.com/ubuntu/pool/main/libr/librsvg
LIBRSVG_LEGACY_LICENSE = LGPL-2.0+
LIBRSVG_LEGACY_LICENSE_FILES = COPYING.LIB
LIBRSVG_LEGACY_INSTALL_STAGING = YES
LIBRSVG_LEGACY_DEPENDENCIES = host-pkgconf cairo gdk-pixbuf libcroco3 libglib2 libxml2 pango
LIBRSVG_LEGACY_CONF_OPTS = --disable-pixbuf-loader --disable-tools \
	--disable-introspection --enable-vala=no --disable-gtk-doc
$(eval $(autotools-package))
