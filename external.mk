include $(sort $(wildcard $(BR2_EXTERNAL_P3LINUX_PATH)/package/*/*.mk))

# Many sources come from older upstream releases (the versions in the Ubuntu
# archive, see scripts/ubuntu-sources.lock) that predate C23, which GCC 15
# uses by default ("bool"/"true" as identifiers, unprototyped functions).
# Build all C code as gnu17; C++ is unaffected.
# GCC 14+ also turned several old-C diagnostics into errors; keep them warnings.
P3_OLDC_FLAGS = -std=gnu17 -Wno-error=incompatible-pointer-types \
	-Wno-error=int-conversion -Wno-error=implicit-function-declaration \
	-Wno-error=implicit-int -fcommon
TARGET_CFLAGS += $(P3_OLDC_FLAGS)
TARGET_CXXFLAGS := $(filter-out $(P3_OLDC_FLAGS),$(TARGET_CXXFLAGS))

# X.Org video drivers built for P3 Linux (see configs/p3linux_defconfig)
P3_XVIDEO = VESA FBDEV CIRRUS MACH64 MGA NEOMAGIC R128 SAVAGE SILICONMOTION TDFX

# Some X.Org components come from older upstream releases (the versions
# available in the Ubuntu archive, see scripts/ubuntu-sources.lock).  Those
# predate C23, which GCC 15 uses by default ("true"/"bool" as identifiers,
# K&R prototypes), so build them as gnu17.  <PKG>_CONF_ENV is expanded when
# the configure recipe runs, so appending here (after the packages) works.
P3_GNU17_PKGS = XLIB_LIBXT XLIB_LIBXEXT XLIB_LIBXMU XLIB_LIBXKBFILE \
	XLIB_LIBXINERAMA XLIB_LIBXXF86VM XLIB_LIBICE XLIB_LIBXFIXES XLIB_LIBXRES \
	XLIB_LIBXPM XLIB_LIBXFT XLIB_LIBXFONT2 XLIB_LIBXAU XAPP_XKBCOMP \
	XAPP_MKFONTSCALE XAPP_BDFTOPCF XTERM $(addprefix XDRIVER_XF86_VIDEO_,$(P3_XVIDEO))
$(foreach p,$(P3_GNU17_PKGS),$(eval $(p)_CONF_ENV += CFLAGS="$$(TARGET_CFLAGS) -std=gnu17"))

# mkfontscale < 1.2 (Ubuntu's version) does not install the mkfontdir wrapper
define P3_HOST_MKFONTDIR_WRAPPER
	printf '#!/bin/sh\nexec "$$(dirname "$$0")/mkfontscale" -b -s -l "$$@"\n' > $(HOST_DIR)/bin/mkfontdir
	chmod 0755 $(HOST_DIR)/bin/mkfontdir
endef
HOST_XAPP_MKFONTSCALE_POST_INSTALL_HOOKS += P3_HOST_MKFONTDIR_WRAPPER

# musl resolves all symbols at dlopen() time unless a module is linked lazily.
# X.Org video drivers reference symbols of sub-modules (int10/vbe, fbdevhw,
# vgahw) that the server loads only later, so link them with -z lazy
# (the toolchain wrapper's -z now comes first; the last -z option wins).
P3_LAZY_PKGS = $(addprefix XDRIVER_XF86_VIDEO_,$(P3_XVIDEO))
$(foreach p,$(P3_LAZY_PKGS),$(eval $(p)_CONF_ENV += LDFLAGS="$$(TARGET_LDFLAGS) -Wl,-z,lazy"))

# Dillo 3.0.5 (Ubuntu archive version): relax GCC 15 template checks and use
# its own name for the TLS option (Buildroot's dillo.mk targets 3.3)
DILLO_CONF_ENV += CXXFLAGS="$(TARGET_CXXFLAGS) -Wno-template-body -fpermissive"
DILLO_CONF_OPTS += --enable-ssl
