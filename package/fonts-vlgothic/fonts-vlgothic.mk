################################################################################
# VL Gothic: compact Japanese TrueType font (M+ / Sazanami based)
################################################################################
FONTS_VLGOTHIC_VERSION = 20220612
FONTS_VLGOTHIC_SOURCE = fonts-vlgothic_$(FONTS_VLGOTHIC_VERSION).orig.tar.xz
FONTS_VLGOTHIC_SITE = http://archive.ubuntu.com/ubuntu/pool/universe/f/fonts-vlgothic
FONTS_VLGOTHIC_LICENSE = mplus, BSD-3-Clause
FONTS_VLGOTHIC_LICENSE_FILES = LICENSE.en LICENSE_E.mplus
define FONTS_VLGOTHIC_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/VL-Gothic-Regular.ttf $(TARGET_DIR)/usr/share/fonts/vlgothic/VL-Gothic-Regular.ttf
	$(INSTALL) -D -m 0644 $(@D)/VL-PGothic-Regular.ttf $(TARGET_DIR)/usr/share/fonts/vlgothic/VL-PGothic-Regular.ttf
endef
$(eval $(generic-package))
