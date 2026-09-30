include $(sort $(wildcard $(BR2_EXTERNAL_P3LINUX_PATH)/package/*/*.mk))

# Some X.Org components come from older upstream releases (the versions
# available in the Ubuntu archive, see scripts/ubuntu-sources.lock).  Those
# predate C23, which GCC 15 uses by default ("true"/"bool" as identifiers,
# K&R prototypes), so build them as gnu17.  <PKG>_CONF_ENV is expanded when
# the configure recipe runs, so appending here (after the packages) works.
P3_GNU17_PKGS = XLIB_LIBXT XLIB_LIBXEXT XLIB_LIBXMU XLIB_LIBXKBFILE \
	XLIB_LIBXINERAMA XLIB_LIBXXF86VM XLIB_LIBICE XLIB_LIBXFIXES XLIB_LIBXRES \
	XLIB_LIBXPM XLIB_LIBXFT XLIB_LIBXFONT2 XLIB_LIBXAU XAPP_XKBCOMP \
	XAPP_MKFONTSCALE XAPP_BDFTOPCF XDRIVER_XF86_VIDEO_FBDEV XTERM
$(foreach p,$(P3_GNU17_PKGS),$(eval $(p)_CONF_ENV += CFLAGS="$$(TARGET_CFLAGS) -std=gnu17"))
