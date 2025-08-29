# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=poemgr
PKG_VERSION:=$(shell git show -s --format=%cd --date=short)
PKG_RELEASE:=1

PKG_MAINTAINER:=David Bauer <mail@david-bauer.net>
PKG_LICENSE:=GPL-2.0-only

PKG_FILE_DEPENDS:=$(CURDIR)/../..

include $(INCLUDE_DIR)/package.mk

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	cp -R $(CURDIR)/../../* $(PKG_BUILD_DIR)
endef

define Package/poemgr
  SECTION:=utils
  CATEGORY:=Utilities
  DEPENDS:=+libuci +libjson-c
  TITLE:=Utility to control PoE ports on the UniFi Flex switch
endef

define Package/poemgr/conffiles
/etc/config/poemgr
endef

define Package/poemgr/install
	$(INSTALL_DIR) $(1)/sbin $(1)/usr/lib/poemgr/config $(1)/etc/config $(1)/etc/uci-defaults $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/poemgr $(1)/sbin/poemgr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/contrib/uswlite-pse-enable $(1)/usr/lib/poemgr/uswlite-pse-enable
	$(CP) $(PKG_BUILD_DIR)/contrib/psx10.config $(1)/usr/lib/poemgr/config/psx10.config
	$(CP) $(PKG_BUILD_DIR)/contrib/usw-lite.config $(1)/usr/lib/poemgr/config/usw-lite.config
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/contrib/uci-defaults.sh $(1)/etc/uci-defaults/99-poemgr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/contrib/procd-init.sh $(1)/etc/init.d/poemgr
endef


$(eval $(call BuildPackage,poemgr))
