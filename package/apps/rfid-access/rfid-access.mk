################################################################################
#
# rfid-access
#
################################################################################

RFID_ACCESS_VERSION = 1.0
RFID_ACCESS_SITE = $(BR2_EXTERNAL_RFID_ACCESS_PATH)/package/apps/rfid-access/src
RFID_ACCESS_SITE_METHOD = local

define RFID_ACCESS_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define RFID_ACCESS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/rfid-access $(TARGET_DIR)/usr/bin/rfid-access
endef

$(eval $(generic-package))