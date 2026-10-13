################################################################################
# gnumeric: spreadsheet (GTK3)
################################################################################
GNUMERIC_VERSION = 1.12.57
GNUMERIC_SOURCE = gnumeric_$(GNUMERIC_VERSION).orig.tar.xz
GNUMERIC_SITE = http://archive.ubuntu.com/ubuntu/pool/universe/g/gnumeric
GNUMERIC_LICENSE = GPL-2.0 or GPL-3.0
GNUMERIC_LICENSE_FILES = COPYING
GNUMERIC_DEPENDENCIES = host-pkgconf host-intltool host-libxslt goffice
GNUMERIC_CONF_OPTS = --disable-nls --disable-introspection --disable-gtk-doc \
	--without-gda --without-psiconv --without-paradox --without-perl \
	--without-python --disable-component --disable-schemas-compile
# itstool only translates the Yelp help, which is not built here
GNUMERIC_CONF_ENV = ITSTOOL=/bin/true
$(eval $(autotools-package))
