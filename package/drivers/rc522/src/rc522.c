#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#define DRIVER_NAME "rc522"
#define CLASS_NAME  "rfid"

static int major;
static struct class *rc522_class;
static struct cdev rc522_cdev;

static int rc522_open(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t rc522_read(struct file *file, char __user *buf,
			  size_t count, loff_t *offset)
{
	/* Simulated UID — will be replaced with actual SPI read */
	const char *uid = "DEADBEEF\n";
	size_t len = strlen(uid);

	if (*offset >= len)
		return 0;

	if (copy_to_user(buf, uid, len))
		return -EFAULT;

	*offset += len;
	return len;
}

static int rc522_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations rc522_fops = {
	.owner   = THIS_MODULE,
	.open    = rc522_open,
	.read    = rc522_read,
	.release = rc522_release,
};

static int rc522_probe(struct spi_device *spi)
{
	dev_t dev;
	int ret;

	ret = alloc_chrdev_region(&dev, 0, 1, DRIVER_NAME);
	if (ret < 0)
		return ret;

	major = MAJOR(dev);
	cdev_init(&rc522_cdev, &rc522_fops);
	ret = cdev_add(&rc522_cdev, dev, 1);
	if (ret < 0)
		goto err_cdev;

	rc522_class = class_create(CLASS_NAME);
	if (IS_ERR(rc522_class)) {
		ret = PTR_ERR(rc522_class);
		goto err_class;
	}

	device_create(rc522_class, NULL, dev, NULL, DRIVER_NAME);
	dev_info(&spi->dev, "RC522 RFID driver probed, major=%d\n", major);
	return 0;

err_class:
	cdev_del(&rc522_cdev);
err_cdev:
	unregister_chrdev_region(dev, 1);
	return ret;
}

static void rc522_remove(struct spi_device *spi)
{
	dev_t dev = MKDEV(major, 0);

	device_destroy(rc522_class, dev);
	class_destroy(rc522_class);
	cdev_del(&rc522_cdev);
	unregister_chrdev_region(dev, 1);
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