/* SPDX-License-Identifier: GPL-2.0-only */

#include "common.h"

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <string.h>
#include <sys/ioctl.h>

int i2c_write(int i2c_fd, uint8_t addr, uint8_t reg, uint8_t data)
{
	struct i2c_rdwr_ioctl_data msg_data[1];
	struct i2c_msg msg[1];
	uint8_t buf[2];

	buf[0] = reg;
	buf[1] = data;

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = buf;

	msg_data[0].msgs = msg;
	msg_data[0].nmsgs = 1;

	return ioctl(i2c_fd, I2C_RDWR, &msg_data);
}

int i2c_read_nbytes(int i2c_fd, uint8_t i2c_addr, uint8_t reg, size_t n,
		    void *val)
{
	struct i2c_rdwr_ioctl_data msg_data[1];
	uint8_t reg_buf[1];

	struct i2c_msg msgs[2] = { 0 };

	/* IP8008 is not able to reponse next Read after Dummy Write
	 * without stop signal. In order to fix this issue,
	 * we add STOP signal after Dummy Write. The flag 'I2C_M_STOP'
	 * can make that.
	 */
	msgs[0].addr = i2c_addr;
	msgs[0].flags = I2C_M_STOP;
	msgs[0].len = 1;
	// First send register
	reg_buf[0] = reg;
	msgs[0].buf = reg_buf;

	msgs[1].addr = i2c_addr;
	msgs[1].flags = I2C_M_RD;
	// Then read n bytes, assume *val has enough space
	msgs[1].len = n;
	memset(val, 0, n);
	msgs[1].buf = val;

	msg_data[0].msgs = msgs;
	msg_data[0].nmsgs = 2;

	if (ioctl(i2c_fd, I2C_RDWR, &msg_data) < 0)
		return -1;

	return 0;
}

int i2c_read(int i2c_fd, uint8_t i2c_addr, uint8_t reg, uint8_t *val)
{
	return i2c_read_nbytes(i2c_fd, i2c_addr, reg, 1, val);
}

int32_t parse_nbit_value_munit(
	int val1,
	int val2,
	int fraction_bits)
{

	/* values must contain 1 byte of data from one register */
	if (val1 > 0xff || val2 > 0xff)
		return -1;

	uint32_t full_data = ((val1 << 8) | (val2));

	uint32_t integral = full_data >> fraction_bits;
	uint32_t fraction_data = (full_data & GENMASK(fraction_bits, 0));

	/* No overflow, since we divide by BIT(fraction_bits)*/
	uint32_t fraction = (1000ULL*fraction_data) / BIT(fraction_bits);

	return integral*1000 + fraction;
}
