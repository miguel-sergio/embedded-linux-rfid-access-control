#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>

#define DRIVER_NAME "rc522"

static int rc522_probe(struct spi_device *spi)
{
    dev_info(&spi->dev, "RC522 RFID driver probed\n");
    return 0;
}

static void rc522_remove(struct spi_device *spi)
{
    dev_info(&spi->dev, "RC522 RFID driver removed\n");
}

static const struct of_device_id rc522_of_match[] = {
    { .compatible = "myproject,rc522" },
    { }
};
MODULE_DEVICE_TABLE(of, rc522_of_match);

static struct spi_driver rc522_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = rc522_of_match,
    },
    .probe  = rc522_probe,
    .remove = rc522_remove,
};

module_spi_driver(rc522_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Miguel");
MODULE_DESCRIPTION("MFRC522 RFID SPI driver");