#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <sys/ioctl.h>

#include "pd69104.h"
#include "pd69104_regs.h"


static int pd69104_wr(struct pd69104_priv *priv, uint8_t reg, uint8_t data)
{
	char reg_data[2] = {reg, data};

	if (write(priv->i2c_fd, &reg_data, 2) != 2) {
		perror("Write error");
		return -1;
	}

	return 0;
}

static int pd69104_rr(struct pd69104_priv *priv, uint8_t reg)
{
	uint8_t buf;

	if (write(priv->i2c_fd, &reg, 1) != 1) {
		perror("Write reg error");
		return -1;
	}

	if (read(priv->i2c_fd, &buf, 1) != 1) {
		perror("Read error");
		return -1;
	}

	return (int) buf;
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
