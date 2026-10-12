################################################################################
# antiword: show the text of Microsoft Word (.doc) documents
################################################################################
ANTIWORD_VERSION = 0.37
ANTIWORD_SOURCE = antiword_$(ANTIWORD_VERSION).orig.tar.gz
ANTIWORD_SITE = http://archive.ubuntu.com/ubuntu/pool/universe/a/antiword
ANTIWORD_LICENSE = GPL-2.0+
ANTIWORD_LICENSE_FILES = Docs/COPYING
define ANTIWORD_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f Makefile.Linux antiword CC="$(TARGET_CC)" \
		LD="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS) -std=gnu17 -DNDEBUG -DGLOBAL_RESOURCES_DIR=\\\"/usr/share/antiword\\\""
endef
define ANTIWORD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/antiword $(TARGET_DIR)/usr/bin/antiword
	mkdir -p $(TARGET_DIR)/usr/share/antiword
	cp -a $(@D)/Resources/. $(TARGET_DIR)/usr/share/antiword/
endef
$(eval $(generic-package))
