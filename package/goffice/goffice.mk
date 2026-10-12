################################################################################
# goffice: office document / chart library (used by Gnumeric)
################################################################################
GOFFICE_VERSION = 0.10.57
GOFFICE_SOURCE = goffice_$(GOFFICE_VERSION).orig.tar.xz
GOFFICE_SITE = http://archive.ubuntu.com/ubuntu/pool/universe/g/goffice
GOFFICE_LICENSE = GPL-2.0 or GPL-3.0
GOFFICE_LICENSE_FILES = COPYING
GOFFICE_INSTALL_STAGING = YES
GOFFICE_DEPENDENCIES = host-pkgconf host-intltool libgtk3 libgsf librsvg-legacy libxslt
GOFFICE_CONF_OPTS = --disable-nls --disable-introspection --disable-gtk-doc \
	--with-lasem=no --with-config-backend=keyfile
$(eval $(autotools-package))
