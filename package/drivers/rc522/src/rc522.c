#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/string.h>

#define DRIVER_NAME "rc522"
#define CLASS_NAME  "rfid"

/*
 * MFRC522 register map.
 * SPI address byte = (reg << 1); bit7=1 for read, bit7=0 for write.
 */
#define REG_COMMAND     0x01  /* starts/stops chip commands */
#define REG_COMIRQ      0x04  /* interrupt request flags */
#define REG_ERROR       0x06  /* error flags from last command */
#define REG_FIFODATA    0x09  /* FIFO buffer R/W */
#define REG_FIFOLEVEL   0x0A  /* FIFO byte count; bit7=FlushBuffer */
#define REG_BITFRAMING  0x0D  /* bits[2:0]: TX last-byte bit count; bit7=StartSend */
#define REG_MODE        0x11  /* bits[1:0]: CRC coprocessor preset */
#define REG_TXMODE      0x12  /* bit7=TxCRCEn, bits[3:0]=TX baud rate */
#define REG_RXMODE      0x13  /* bit7=RxCRCEn, bits[3:0]=RX baud rate */
#define REG_TXCONTROL   0x14  /* bits[1:0]: enable TX1/TX2 RF output pins */
#define REG_TXASK       0x15  /* bit6=Force100ASK: force 100% ASK modulation */
#define REG_RFCFG       0x26  /* bits[6:4]: receiver gain (111b = max ~48 dB) */
#define REG_TMODE       0x2A  /* bit7=TAuto; bits[3:0]=prescaler[11:8] */
#define REG_TPRESCALER  0x2B  /* timer prescaler[7:0] */
#define REG_TRELOADH    0x2C  /* timer reload value [15:8] */
#define REG_TRELOADL    0x2D  /* timer reload value [7:0] */
#define REG_VERSION     0x37  /* chip version (0x91/0x92 for genuine MFRC522) */

/* CommandReg values */
#define CMD_IDLE        0x00
#define CMD_TRANSCEIVE  0x0C
#define CMD_SOFTRESET   0x0F

/* ComIrqReg bits */
#define IRQ_TIMER   BIT(0)  /* hardware timer reached 0 → no card response */
#define IRQ_ERR     BIT(1)  /* error bit set in ErrorReg */
#define IRQ_IDLE    BIT(4)  /* command terminated (also fires on error) */
#define IRQ_RX      BIT(5)  /* receiver detected end of a valid data stream */

/* ErrorReg bits */
#define ERR_PROTO   BIT(0)  /* SOF or EOF violation */
#define ERR_PARITY  BIT(1)
#define ERR_CRC     BIT(2)
#define ERR_COLL    BIT(3)  /* bit collision detected */
#define ERR_OVFL    BIT(4)  /* FIFO overflowed */

static int major;
static struct class *rc522_class;
static struct cdev rc522_cdev;

struct rc522_dev {
	struct spi_device *spi;
};

static struct rc522_dev *rc522_devdata;

static u8 rc522_read_reg(struct spi_device *spi, u8 reg)
{
	/* Address byte: (reg<<1) | 0x80 sets bit7=1 for read; second byte is dummy */
	u8 tx[2] = { ((reg << 1) & 0x7E) | 0x80, 0xFF };
	u8 rx[2] = { 0 };
	struct spi_transfer t = { .tx_buf = tx, .rx_buf = rx, .len = 2 };
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	spi_sync(spi, &m);
	return rx[1];
}

static void rc522_write_reg(struct spi_device *spi, u8 reg, u8 val)
{
	/* Address byte: (reg<<1) with bit7=0 for write */
	u8 tx[2] = { (reg << 1) & 0x7E, val };
	struct spi_transfer t = { .tx_buf = tx, .len = 2 };
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	spi_sync(spi, &m);
}

static void rc522_set_bits(struct spi_device *spi, u8 reg, u8 mask)
{
	rc522_write_reg(spi, reg, rc522_read_reg(spi, reg) | mask);
}

static void rc522_clear_bits(struct spi_device *spi, u8 reg, u8 mask)
{
	rc522_write_reg(spi, reg, rc522_read_reg(spi, reg) & ~mask);
}

/*
 * Send bytes over RF and wait for card response.
 *
 * Caller must set REG_BITFRAMING[2:0] before calling:
 *   0x07 → 7-bit last byte (REQA short frame)
 *   0x00 → full 8-bit bytes (everything else)
 *
 * Hardware timer (TAuto=1) starts after the last TX bit and fires after ~25 ms
 * if no response arrives. Software cap is set above that.
 */
static int rc522_transceive(struct spi_device *spi,
			    const u8 *send, u8 send_len,
			    u8 *recv, u8 *recv_len)
{
	int i;
	u8 irq, err;

	/* Flush FIFO (bit7=FlushBuffer), clear all IRQ flags, go idle */
	rc522_write_reg(spi, REG_FIFOLEVEL, 0x80);
	rc522_write_reg(spi, REG_COMIRQ, 0x7F);  /* bit7=0 → clear-mode; [6:0]=1 → clear all */
	rc522_write_reg(spi, REG_COMMAND, CMD_IDLE);

	for (i = 0; i < send_len; i++)
		rc522_write_reg(spi, REG_FIFODATA, send[i]);

	/*
	 * Arm Transceive then assert StartSend (REG_BITFRAMING bit7).
	 * Command must be written first so the chip is in transceive state
	 * before StartSend kicks off RF transmission.
	 */
	rc522_write_reg(spi, REG_COMMAND, CMD_TRANSCEIVE);
	rc522_set_bits(spi, REG_BITFRAMING, 0x80);

	/*
	 * Poll ComIrqReg. Exit conditions:
	 *   IRQ_RX   (bit5) → data received, success
	 *   IRQ_IDLE (bit4) → command ended (may also mean error)
	 *   IRQ_ERR  (bit1) → error during receive
	 *   IRQ_TIMER(bit0) → 25 ms elapsed with no response → no card
	 *
	 * Soft cap: 40 × 1 ms = 40 ms > 25 ms hardware timeout.
	 */
	for (i = 0; i < 40; i++) {
		irq = rc522_read_reg(spi, REG_COMIRQ);
		if (irq & (IRQ_RX | IRQ_IDLE | IRQ_ERR | IRQ_TIMER))
			break;
		usleep_range(900, 1100);
	}

	/* Deactivate StartSend and stop command before inspecting result */
	rc522_clear_bits(spi, REG_BITFRAMING, 0x80);
	rc522_write_reg(spi, REG_COMMAND, CMD_IDLE);

	if (irq & IRQ_TIMER)
		return -ETIMEDOUT;  /* hardware timeout: no card in field */

	if (!(irq & IRQ_RX))
		return -ETIMEDOUT;  /* software cap reached without data */

	err = rc522_read_reg(spi, REG_ERROR);
	if (err & (ERR_OVFL | ERR_CRC | ERR_PARITY | ERR_PROTO))
		return -EIO;

	/* REG_FIFOLEVEL[5:0] = number of bytes in FIFO */
	*recv_len = rc522_read_reg(spi, REG_FIFOLEVEL) & 0x3F;
	if (*recv_len == 0)
		return -EIO;
	if (*recv_len > 10)
		*recv_len = 10;

	for (i = 0; i < *recv_len; i++)
		recv[i] = rc522_read_reg(spi, REG_FIFODATA);

	return 0;
}

/*
 * ISO 14443-A card detection sequence:
 *   1. REQA (7-bit 0x26)          → ATQA (2 bytes): card present
 *   2. ANTICOLLISION (0x93 0x20)  → 5 bytes: UID[0..3] + BCC
 */
static int rc522_read_uid(struct spi_device *spi, u8 *uid, u8 *uid_len)
{
	u8 send[2], recv[10];
	u8 recv_len;
	int ret;

	/* Step 1 – REQA: 7-bit short frame, no CRC */
	rc522_write_reg(spi, REG_BITFRAMING, 0x07);
	send[0] = 0x26;
	ret = rc522_transceive(spi, send, 1, recv, &recv_len);
	if (ret)
		return ret;  /* -ETIMEDOUT means no card present */

	/* Step 2 – ANTICOLLISION cascade level 1: NVB=0x20 → 0 known UID bits */
	rc522_write_reg(spi, REG_BITFRAMING, 0x00);
	send[0] = 0x93;
	send[1] = 0x20;
	ret = rc522_transceive(spi, send, 2, recv, &recv_len);
	if (ret)
		return ret;

	/* recv[0..3]=UID bytes, recv[4]=BCC (XOR of UID bytes, integrity check) */
	if (recv_len < 5)
		return -EIO;
	if ((recv[0] ^ recv[1] ^ recv[2] ^ recv[3] ^ recv[4]) != 0)
		return -EIO;  /* BCC mismatch: corrupted data */

	memcpy(uid, recv, 4);
	*uid_len = 4;
	return 0;
}

static int rc522_open(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t rc522_read(struct file *file, char __user *buf,
			  size_t count, loff_t *offset)
{
	char uid_str[32];
	u8 uid[10];
	u8 uid_len = 0;
	int ret, i, len;

	if (*offset > 0)
		return 0;

	ret = rc522_read_uid(rc522_devdata->spi, uid, &uid_len);
	if (ret)
		return ret;

	len = 0;
	for (i = 0; i < uid_len; i++)
		len += snprintf(uid_str + len, sizeof(uid_str) - len,
				"%02X", uid[i]);
	uid_str[len++] = '\n';

	if (copy_to_user(buf, uid_str, len))
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
	u8 version;

	ret = alloc_chrdev_region(&dev, 0, 1, DRIVER_NAME);
	if (ret < 0)
		return ret;

	/* MFRC522 supports SPI mode 0, up to 10 MHz; 5 MHz for margin */
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	spi->max_speed_hz = 5000000;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
		goto err_cdev;
	}

	/* Soft reset; datasheet requires waiting for oscillator to settle */
	rc522_write_reg(spi, REG_COMMAND, CMD_SOFTRESET);
	msleep(50);

	/*
	 * ISO 14443-A mandatory configuration (all differ from reset defaults):
	 *
	 *   REG_MODE   0x3D → CRC preset = 0x6363 (ISO 14443-3 §6.2.4);
	 *                      reset default 0x3F uses preset 0xFFFF (wrong)
	 *   REG_TXASK  0x40 → Force100ASK = 1; mandatory for ISO 14443-A;
	 *                      reset default = 0 (modulation depth uncontrolled)
	 *   REG_RFCFG  0x70 → RxGain[2:0] = 111b → max receiver gain (~48 dB);
	 *                      reset default = 0x48 (lower gain, misses weak cards)
	 *   REG_TXMODE 0x00 → 106 kBd, no CRC appended on TX
	 *   REG_RXMODE 0x00 → 106 kBd, no CRC stripped on RX
	 */
	rc522_write_reg(spi, REG_MODE,    0x3D);
	rc522_write_reg(spi, REG_TXASK,   0x40);
	rc522_write_reg(spi, REG_RFCFG,   0x70);
	rc522_write_reg(spi, REG_TXMODE,  0x00);
	rc522_write_reg(spi, REG_RXMODE,  0x00);

	/*
	 * Response timeout timer (TAuto=1 → starts automatically after last TX bit):
	 *   prescaler = 0x0A9 = 169 → f_timer = 13.56 MHz / (2×170) ≈ 40 kHz
	 *   reload    = 0x3E8 = 1000 → timeout = 1000 / 40 kHz = 25 ms
	 */
	rc522_write_reg(spi, REG_TMODE,      0x80);
	rc522_write_reg(spi, REG_TPRESCALER, 0xA9);
	rc522_write_reg(spi, REG_TRELOADH,   0x03);
	rc522_write_reg(spi, REG_TRELOADL,   0xE8);

	/* TX1 and TX2 RF output pins are disabled after reset; enable both */
	rc522_set_bits(spi, REG_TXCONTROL, 0x03);

	version = rc522_read_reg(spi, REG_VERSION);
	dev_info(&spi->dev, "MFRC522 probed, version=0x%02x\n", version);

	rc522_devdata = devm_kzalloc(&spi->dev, sizeof(*rc522_devdata),
				     GFP_KERNEL);
	if (!rc522_devdata) {
		ret = -ENOMEM;
		goto err_cdev;
	}
	rc522_devdata->spi = spi;

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