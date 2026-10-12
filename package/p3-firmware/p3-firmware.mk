################################################################################
#
# p3-firmware: device firmware from linux-firmware (pinned commit)
#
################################################################################

P3_FIRMWARE_VERSION = afabaf773c4c2e2c841429933a6a084a5af4d14d
P3_FIRMWARE_SOURCE =
P3_FIRMWARE_LICENSE = Redistributable firmware (see WHENCE)
P3_FIRMWARE_FILES = \
	radeon/R100_cp.bin \
	radeon/R200_cp.bin \
	radeon/R300_cp.bin \
	radeon/R420_cp.bin \
	radeon/R520_cp.bin \
	radeon/RS600_cp.bin \
	radeon/RS690_cp.bin \
	rt2561.bin \
	rt2561s.bin \
	rt2661.bin \
	rt73.bin \
	rt2860.bin \
	rt2870.bin \
	rt3071.bin \
	htc_9271.fw \
	htc_7010.fw \
	ath9k_htc/htc_9271-1.4.0.fw \
	ath9k_htc/htc_7010-1.4.0.fw \
	carl9170-1.fw \
	ar5523.bin \
	libertas/usb8388_v9.bin \
	libertas/usb8682.bin \
	lbtf_usb.bin \
	rtl_nic/rtl8168d-1.fw \
	rtl_nic/rtl8168d-2.fw \
	rtl_nic/rtl8168e-1.fw \
	rtl_nic/rtl8168e-2.fw \
	rtl_nic/rtl8168e-3.fw \
	rtl_nic/rtl8105e-1.fw \
	e100/d101m_ucode.bin \
	e100/d101s_ucode.bin \
	e100/d102e_ucode.bin \
	tigon/tg3.bin \
	tigon/tg3_tso.bin \
	tigon/tg3_tso5.bin \
	3com/typhoon.bin \
	WHENCE

P3_FIRMWARE_URL = https://gitlab.com/kernel-firmware/linux-firmware/-/raw/$(P3_FIRMWARE_VERSION)
P3_FIRMWARE_EXTRA_DOWNLOADS = $(addprefix $(P3_FIRMWARE_URL)/,$(P3_FIRMWARE_FILES))

define P3_FIRMWARE_INSTALL_TARGET_CMDS
	$(foreach f,$(P3_FIRMWARE_FILES),\
		$(INSTALL) -D -m 0644 $(P3_FIRMWARE_DL_DIR)/$(notdir $(f)) $(TARGET_DIR)/lib/firmware/$(f)$(sep))
	ln -sf rt2860.bin $(TARGET_DIR)/lib/firmware/rt3090.bin
	ln -sf rt2870.bin $(TARGET_DIR)/lib/firmware/rt3070.bin
endef

$(eval $(generic-package))
