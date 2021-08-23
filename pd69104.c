#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <sys/ioctl.h>

#include "pd69104.h"
#include "pd69104_regs.h"

#define I2C_SMBUS_READ	1
#define I2C_SMBUS_WRITE	0


static int32_t i2c_smbus_access(int file, char read_write, uint8_t command, int size, char *data)
{
	struct i2c_smbus_ioctl_data args;

	args.read_write = read_write;
	args.command = command;
	args.size = size;
	args.data = data;
	return ioctl(file,I2C_SMBUS,&args);
}


static int pd69104_wr(struct pd69104_priv *priv, uint8_t reg, uint8_t val)
{
	union i2c_smbus_data data;

	data.byte = val;

	return i2c_smbus_access(priv->i2c_fd, I2C_SMBUS_WRITE, reg,
							2, (char *) &data);
}

static int pd69104_rr(struct pd69104_priv *priv, uint8_t reg)
{
	union i2c_smbus_data data;

	if (i2c_smbus_access(priv->i2c_fd, I2C_SMBUS_READ, reg, 2, (char *) &data))
		return -1;
	
	return 0x0FF & data.byte;
}

int pd69104_init(struct pd69104_priv *priv, int i2c_bus, int i2c_addr)
{
	char i2cpath[30];
	int fd;

	priv->i2c_addr = i2c_addr;

	snprintf(i2cpath, 30, "/dev/i2c-%d", i2c_bus);

	fd = open(i2cpath, O_RDWR);

	if (fd == -1) {
		perror(i2cpath);
		return 1;
	}

	if (ioctl(fd, I2C_SLAVE, priv->i2c_addr) < 0) {
		perror("i2c_set_address");
		return 1;
	}

	priv->i2c_fd = fd;

	return 0;
}

int pd69104_end(struct pd69104_priv *priv)
{
	return !!close(priv->i2c_fd);
}

int pd69104_port_power_consumption_get(struct pd69104_priv *priv, int port)
{
	return pd69104_rr(priv, PD69104_REG_PORT_CONS(port));
}

int pd69104_pwrgd_pin_status_get(struct pd69104_priv *priv, int port)
{
	return (pd69104_rr(priv, PD69104_REG_PWRGD) & PD69104_REG_PWRGD_PIN_STATUS_MASK) >> PD69104_REG_PWRGD_PIN_STATUS_SHIFT;
}
