################################################################################
# catdoc: text from MS Word (.doc), Excel (xls2csv) and PowerPoint (catppt)
################################################################################
CATDOC_VERSION = 0.95
CATDOC_SOURCE = catdoc_$(CATDOC_VERSION).orig.tar.gz
CATDOC_SITE = http://archive.ubuntu.com/ubuntu/pool/universe/c/catdoc
CATDOC_LICENSE = GPL-2.0+
CATDOC_LICENSE_FILES = COPYING
CATDOC_CONF_OPTS = --disable-wordview --without-wish
CATDOC_CONF_ENV = CFLAGS="$(TARGET_CFLAGS) -std=gnu17"
CATDOC_MAKE_OPTS = -j1
$(eval $(autotools-package))
