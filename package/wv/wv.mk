################################################################################
# wv: Microsoft Word document import library (used by AbiWord)
################################################################################
WV_VERSION = 1.2.9
WV_SOURCE = wv_$(WV_VERSION).orig.tar.gz
WV_SITE = http://archive.ubuntu.com/ubuntu/pool/universe/w/wv
WV_LICENSE = GPL-2.0+
WV_LICENSE_FILES = COPYING
WV_INSTALL_STAGING = YES
WV_DEPENDENCIES = host-pkgconf libgsf libpng zlib
WV_CONF_OPTS = --without-libwmf
$(eval $(autotools-package))
