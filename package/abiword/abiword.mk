################################################################################
# abiword: word processor (GTK3)
################################################################################
ABIWORD_VERSION = 3.0.8
ABIWORD_SOURCE = abiword_$(ABIWORD_VERSION)+ds.orig.tar.xz
ABIWORD_SITE = http://archive.ubuntu.com/ubuntu/pool/universe/a/abiword
ABIWORD_LICENSE = GPL-2.0+
ABIWORD_LICENSE_FILES = COPYING
ABIWORD_DEPENDENCIES = host-pkgconf boost libfribidi libgsf libgtk3 \
	librsvg-legacy libxslt wv
ABIWORD_AUTORECONF = YES
ABIWORD_CONF_OPTS = --disable-default-plugins \
	--enable-plugins="opendocument openxml" \
	--disable-spell --disable-print --without-goffice --without-gio \
	--without-redland --without-evolution-data-server --without-libical \
	--without-champlain --disable-introspection \
	--with-boost=$(STAGING_DIR)/usr
ABIWORD_CONF_ENV = CXXFLAGS="$(TARGET_CXXFLAGS) -std=gnu++14 -fpermissive"

# the source is a git tag snapshot: generate the plugin lists like autogen.sh
define ABIWORD_AUTOGEN_COMMON
	cd $(@D) && ./autogen-common.sh
endef
ABIWORD_PRE_CONFIGURE_HOOKS += ABIWORD_AUTOGEN_COMMON
$(eval $(autotools-package))
