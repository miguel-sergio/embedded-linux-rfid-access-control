################################################################################
#
# rc522
#
################################################################################

RC522_VERSION = 1.0
RC522_SITE = $(BR2_EXTERNAL_RFID_ACCESS_PATH)/package/drivers/rc522/src
RC522_SITE_METHOD = local

define RC522_INSTALL_TARGET_CMDS
	$(MAKE) -C $(LINUX_DIR) M=$(@D) \
		INSTALL_MOD_PATH=$(TARGET_DIR) \
		modules_install
endef

$(eval $(kernel-module))
$(eval $(generic-package))
