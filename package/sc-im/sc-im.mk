################################################################################
# sc-im: spreadsheet calculator (vim-like keys, CSV/XLSX), runs in a terminal
################################################################################
SC_IM_VERSION = 0.8.5+ds
SC_IM_SOURCE = sc-im_$(SC_IM_VERSION).orig.tar.xz
SC_IM_SITE = http://archive.ubuntu.com/ubuntu/pool/universe/s/sc-im
SC_IM_LICENSE = BSD-4-Clause
SC_IM_LICENSE_FILES = LICENSE
SC_IM_DEPENDENCIES = ncurses host-pkgconf host-bison
define SC_IM_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/src CC="$(TARGET_CC)" prefix=/usr \
		CFLAGS="$(TARGET_CFLAGS) -std=gnu17" PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" YACC="bison -y"
endef
define SC_IM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/src/sc-im $(TARGET_DIR)/usr/bin/sc-im
	$(INSTALL) -D -m 0644 $(@D)/src/doc $(TARGET_DIR)/usr/share/sc-im/sc-im_help
endef
$(eval $(generic-package))
