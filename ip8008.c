/* SPDX-License-Identifier: GPL-2.0-only */

#include "ip8008.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "common.h"

#define I2C_SMBUS_READ 1
#define I2C_SMBUS_WRITE 0

#define IP8008_COMM_COOLDOWN (0)

static struct ip8008_priv *ip8008_priv(struct poemgr_pse_chip *pse_chip) {
	return (struct ip8008_priv *) pse_chip->priv;
}

static int ip8008_wr(struct poemgr_pse_chip *pse_chip, uint8_t reg, uint8_t val)
{
	struct ip8008_priv *priv = ip8008_priv(pse_chip);

	return i2c_write(priv->i2c_fd, priv->i2c_addr, reg, val);
}

static int ip8008_rr(struct poemgr_pse_chip *pse_chip, uint8_t reg)
{
	struct ip8008_priv *priv = ip8008_priv(pse_chip);
	uint8_t result = 0;

	if (i2c_read(priv->i2c_fd, priv->i2c_addr, reg, &result) < 0)
		return -1;

	return result;
}

static int ip8008_masked_wr(struct poemgr_pse_chip *pse_chip, uint8_t reg, uint8_t mask, uint8_t newval)
{
	int ret = -1, val;

	val = ip8008_rr(pse_chip, reg);
	if (val < 0)
		goto out;

	val &= (~mask);
	val |= newval;

	ret = ip8008_wr(pse_chip, reg, val);
out:
	return ret;
}

static inline int ip8008_switch_page(struct poemgr_pse_chip *pse_chip, uint8_t page)
{
	/* valid page value is either 0, 1 or 2 */
	page &= 0b11;

	return ip8008_masked_wr(pse_chip,
		IP8008_REG_SETPAGE,
		IP8008_MASK_SETPAGE,
		0x4 | (page << IP8008_OFFSET_SETPAGE));
}


static int ip8008_paged_read(struct poemgr_pse_chip *pse_chip, uint8_t page, uint8_t reg)
{
	int ret;

	ret = ip8008_switch_page(pse_chip, page);
	if (ret < 0)
		goto out;

	ret = ip8008_rr(pse_chip, reg);

out:
	return ret;
}


static int ip8008_paged_write(struct poemgr_pse_chip *pse_chip, uint8_t page, uint8_t reg, uint8_t val)
{
	int ret;

	ret = ip8008_switch_page(pse_chip, page);
	if (ret < 0)
		goto out;

	ret = ip8008_wr(pse_chip, reg, val);
	if (ret < 0)
		goto out;

	ret = 0;
out:
	return ret;
}

static int ip8008_masked_paged_write(struct poemgr_pse_chip *pse_chip, uint8_t page, uint8_t reg, uint8_t mask, uint8_t newval)
{
	int ret = ip8008_switch_page(pse_chip, page);
	if (ret < 0)
		goto out;

	ret = ip8008_masked_wr(pse_chip, reg, mask, newval);
	if (ret < 0)
		goto out;

out:
	return ret;
}

int ip8008_device_online(struct poemgr_pse_chip *pse_chip)
{
	int ret = -1;
	int msb, lsb;
	uint16_t device_id;

	/* Check device ID */
	ret = msb = ip8008_paged_read(pse_chip, IP8008_PAGE_DEVICE_ID, IP8008_REG_DEVICE_ID_MSB);
	if (ret < 0)
		goto out;
	ret = lsb = ip8008_paged_read(pse_chip, IP8008_PAGE_DEVICE_ID, IP8008_REG_DEVICE_ID_LSB);
	if (ret < 0)
		goto out;

	device_id = IP8008_COMBINE_BYTES(msb, lsb);

#if defined(ENABLE_DEBUG)
	debug("device id: %04x\n", device_id);
#endif

	if (device_id != 0x3801) {
		debug("Unknown device id: %04x\n Expected: %04x\n", device_id, 0x3804);
		ret = -1;
		goto out;
	}

#if defined(ENABLE_DEBUG)
	ret = ip8008_paged_read(pse_chip, 0x0, 0x68);
	debug("IVT Temperature Event Handler status: %lu\n", ret & BIT(7));
	debug("IVT Voltage Event Handler status: %lu\n", ret & BIT(6));
	debug("IVT Current Event Handler status: %lu\n", ret & BIT(5));
#endif

	ret = 0;
out:
	return ret;
}

int ip8008_device_enable_set(struct poemgr_pse_chip *pse_chip, int is_enabled)
{
	int ret = -1;

	ret = ip8008_masked_paged_write(
		pse_chip,
		IP8008_PAGE_IVT_POLL,
		IP8008_REG_IVT_POLL,
		IP8008_BIT_AUTO_POLL | IP8008_MASK_IVT_POLL_INTERVAL,
		is_enabled ? (IP8008_BIT_AUTO_POLL | IP8008_MASK_IVT_POLL_INTERVAL) : 0);
	if (ret < 0)
		goto out;

	/* Enable class based power mode as per OEM */
	ret = ip8008_masked_paged_write(pse_chip,
			IP8008_PAGE_POWER_ALLOC_MODE,
			IP8008_REG_POWER_ALLOC_MODE,
			IP8008_MASK_POWER_ALLOC_MODE,
			is_enabled ? (0b01 << IP8008_OFFSET_POWER_ALLOC_MODE) : 0);

	if (ret < 0)
		goto out;

	ret = ip8008_paged_write(pse_chip, IP8008_PAGE_BT_MODE, IP8008_REG_BT_MODE, is_enabled ? 0xf0 : 0);

out:
	return ret;
}

int ip8008_port_enable_set(struct poemgr_pse_chip *pse_chip, int port, int enable_disable)
{
	int ret = -1;

	uint8_t val = !!enable_disable;

	ret = ip8008_paged_write(pse_chip, IP8008_PAGE_POWER_CONTROL, IP8008_REG_POWER_CONTROL(port), val);
	return ret;
}


int ip8008_port_enable_get(struct poemgr_pse_chip *pse_chip, int port)
{
	int ret = -1;

	ret = ip8008_switch_page(pse_chip, IP8008_PAGE_POWER_CONTROL);
	if (ret < 0)
		goto out;

	ret = ip8008_rr(pse_chip, IP8008_REG_POWER_CONTROL(port));
	if (ret < 0)
		goto out;

	ret = (ret & IP8008_MASK_PSE_ENABLE) == 0b01;
out:
	return ret;
}

int ip8008_power_budget_set(struct poemgr_pse_chip *pse_chip, int watts)
{
	int ret = -1;

	ret = ip8008_masked_paged_write(pse_chip, IP8008_PAGE_POWER_REGISTER, IP8008_REG_POWER_REGISTER, 0b11, 0b01);
	if (ret < 0)
		goto out;


	/* Trunk 0 power limit */
	ret = ip8008_paged_write(pse_chip, 0, 0xd8, 0x00);
	if (ret < 0)
		goto out;
	ret = ip8008_paged_write(pse_chip, 0, 0xd9, watts >> 8);
	if (ret < 0)
		goto out;
	ret = ip8008_paged_write(pse_chip, 0, 0xda, watts & 0xff);
	if (ret < 0)
		goto out;

	/* Trunk 1 power limit */
	ret = ip8008_paged_write(pse_chip, 0, 0xd8, 0x01);
	if (ret < 0)
		goto out;
	ret = ip8008_paged_write(pse_chip, 0, 0xdb, watts >> 8);
	if (ret < 0)
		goto out;
	ret = ip8008_paged_write(pse_chip, 0, 0xdc, watts & 0xff);
	if (ret < 0)
		goto out;

	ret = ip8008_paged_write(pse_chip, IP8008_PAGE_VICTIM_STRATEGY, IP8008_REG_VICTIM_STRATEGY, 0x0);
	if (ret < 0)
		goto out;

	if (watts > GENMASK(11, 0)) {
		debug("Power limit: %d exceeds maximum value: %lu\n", watts, GENMASK(11, 0));
		ret = -5;
		goto out;
	}

	ret = ip8008_paged_write(pse_chip, IP8008_PAGE_AVAILABLE_CURRENT, IP8008_REG_AVAILABLE_CURRENT, (watts << 2) >> 8);
	if (ret < 0)
		goto out;

	ret = ip8008_paged_write(pse_chip, IP8008_PAGE_AVAILABLE_CURRENT, IP8008_REG_AVAILABLE_CURRENT + 1, (watts << 2) & 0xff);
	if (ret < 0)
		goto out;

	ret = ip8008_paged_write(pse_chip, 0, 0xea, watts >> 8);
	if (ret < 0)
		goto out;

	ret = ip8008_paged_write(pse_chip, 0, 0xeb, watts & 0xff);

out:
	return ret;
}

int ip8008_port_power_consumption_get(struct poemgr_pse_chip *pse_chip, int port)
{
	int ret = -1;

	int val1, val2;
	ret = val1 = ip8008_paged_read(pse_chip, IP8008_PAGE_CONSUMED_POWER, IP8008_REG_CONSUMED_POWER(port));
	if (ret < 0)
		goto out;
	ret = val2 = ip8008_paged_read(pse_chip, IP8008_PAGE_CONSUMED_POWER, IP8008_REG_CONSUMED_POWER(port) + 1);
	if (ret < 0)
		goto out;

	ret = parse_nbit_value_munit(val1 & GENMASK(2, 0), val2, 4);

out:
	return ret;
}

int ip8008_port_good_get(struct poemgr_pse_chip *pse_chip, int port)
{
	int ret = -1;

	ret = ip8008_paged_read(pse_chip, IP8008_PAGE_PORT_POWER_STATUS, IP8008_REG_PORT_POWER_STATUS);
	if (ret < 0)
		goto out;

	ret = !!(ret & BIT(port));

out:
	return ret;
}

int ip8008_port_power_limit_get(struct poemgr_pse_chip *pse_chip, int port)
{
	int ret = -1;

	// TODO

	ret = 0;

	return ret;
}

int ip8008_port_faults_get(struct poemgr_pse_chip *pse_chip, int port)
{
	int ret = -1;
	int status0;

	ret = status0 = ip8008_paged_read(pse_chip, IP8008_PAGE_STATUS, IP8008_REG_STATUS(port));
	if (ret < 0)
		goto out;

	ret = 0;

	if (status0 == 0)
		goto out;

	if (status0 & BIT(0))
		ret |= POEMGR_FAULT_TYPE_OVER_CURRENT;
	if (status0 & BIT(1))
		ret |= POEMGR_FAULT_TYPE_SHORT_CIRCUIT;
	if (status0 & BIT(4))
		ret |= POEMGR_FAULT_TYPE_OVER_TEMPERATURE;
	if (status0 & BIT(5))
		ret |= POEMGR_FAULT_TYPE_SHORT_CIRCUIT;
	if (status0 & BIT(7))
		ret |= POEMGR_FAULT_TYPE_POWER_MANAGEMENT;

	if (status0 & BIT(2) ||
		status0 & BIT(3) ||
		status0 & BIT(6))
		ret |= POEMGR_FAULT_TYPE_UNKNOWN;

out:
	return ret;
}

int ip8008_port_clear_faults(struct poemgr_pse_chip *pse_chip, int num_ports)
{
	int ret = -1;

	for (int i = 0; i < num_ports; i++) {
		ip8008_paged_write(pse_chip, IP8008_PAGE_STATUS, IP8008_REG_STATUS(i), 0xff);
		ip8008_paged_write(pse_chip, IP8008_PAGE_STATUS, IP8008_REG_STATUS(i) + 1, 0xff);
	}

	ret = ip8008_paged_write(pse_chip, 0, 0x0f, 0b01111111);
	if (ret < 0)
		goto out;

	ret = ip8008_paged_write(pse_chip, 0, 0x0f, 0);


out:
	return ret;
}

int ip8008_port_poe_class_get(struct poemgr_pse_chip *pse_chip, int port)
{
	int ret = -1;

	ret = ip8008_paged_read(pse_chip, IP8008_PAGE_REQ_CLASS_STATUS, IP8008_REG_REQ_CLASS_STATUS(port));
	if (ret < 0)
		goto out;

	ret &= GENMASK(3, 0);

out:
	return ret;
}

int ip8008_export_port_metric(struct poemgr_pse_chip *pse_chip, int port, struct poemgr_metric *output, int metric)
{
	int ret = -1, msb, lsb;

	if (metric < 0)
		return -1;

	if (metric == 0) {
		ret = msb = ip8008_paged_read(pse_chip, IP8008_PAGE_CURRENT_STATUS, IP8008_REG_CURRENT_STATUS(port));
		if (ret < 0)
			goto out;
		ret = lsb = ip8008_paged_read(pse_chip, IP8008_PAGE_CURRENT_STATUS, IP8008_REG_CURRENT_STATUS(port) + 1);
		if (ret < 0)
			goto out;
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Current";
		output->val_uint32 = parse_nbit_value_munit(msb, lsb, 2) / 1000;

	} else if (metric == 1) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Voltage";

		/* Supply voltage */
		ret = msb = ip8008_paged_read(pse_chip, 0x0, 0x8e);
		if (ret < 0)
			goto out;
		ret = lsb = ip8008_paged_read(pse_chip, 0x0, 0x8f);
		if (ret < 0)
			goto out;
		output->val_uint32 = parse_nbit_value_munit(msb, lsb, 4);

		/* Consumed Voltage */
		ret = msb = ip8008_paged_read(pse_chip, IP8008_PAGE_VOLTAGE_STATUS, IP8008_REG_VOLTAGE_STATUS(port));
		if (ret < 0)
			goto out;
		ret = lsb = ip8008_paged_read(pse_chip, IP8008_PAGE_VOLTAGE_STATUS, IP8008_REG_VOLTAGE_STATUS(port) + 1);
		if (ret < 0)
			goto out;

		/* Total voltage = Supply voltage - Consumed Voltage */
		output->val_uint32 -= parse_nbit_value_munit(msb, lsb, 4);

	} else if (metric == 2) {
		ret = msb = ip8008_paged_read(pse_chip, IP8008_PAGE_TEMP_STATUS, IP8008_REG_TEMP_STATUS(port));
		if (ret < 0)
			goto out;
		ret = lsb = ip8008_paged_read(pse_chip, IP8008_PAGE_TEMP_STATUS, IP8008_REG_TEMP_STATUS(port) + 1);
		if (ret < 0)
			goto out;
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Temperature";
		output->val_uint32 = parse_nbit_value_munit(msb, lsb, 4);

	} else {
		output->type = POEMGR_METRIC_END;
	}

	ret = 0;
out:
	return ret;
}

static int ip8008_export_metric(struct poemgr_pse_chip *pse_chip, struct poemgr_metric *output, int metric)
{
	int ret = -1, msb, lsb;
	if (metric < 0 || metric >= pse_chip->num_metrics)
		return -1;

	if (metric == 0) {
		ret = ip8008_paged_read(pse_chip, 0x0, 0xd8);
		output->type = POEMGR_METRIC_INT32;
		output->name = "Trunk";
		output->val_int32 = ret;

	} else if (metric == 1) {
		msb = ip8008_paged_read(pse_chip, 0x0, 0xd9);
		lsb = ip8008_paged_read(pse_chip, 0x0, 0xda);
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Trunk 0 Power Limit";
		output->val_uint32 = ((msb << 8) | lsb) * 1000;

	} else if (metric == 2) {
		msb = ip8008_paged_read(pse_chip, 0x0, 0xdb);
		lsb = ip8008_paged_read(pse_chip, 0x0, 0xdc);
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Trunk 1 Power Limit";
		output->val_uint32 = ((msb << 8) | lsb) * 1000;

	} else if (metric == 3) {
		ret = ip8008_paged_read(pse_chip, 0x0, 0x6c);
		output->type = POEMGR_METRIC_STRING;
		output->name = "Estimation mode";
		switch (ret & 0b11) {
		case 0:
			snprintf(output->val_char, sizeof(output->val_char), "Idea Class Power");
			break;
		case 1:
			snprintf(output->val_char, sizeof(output->val_char), "Real Consumed Power");
			break;
		case 2:
			snprintf(output->val_char, sizeof(output->val_char), "Max Consumed Power");
			break;
		case 3:
			snprintf(output->val_char, sizeof(output->val_char), "Real Consumed Power and Auto Class Power");
			break;
		}

	} else if (metric == 4) {
		ret = ip8008_paged_read(pse_chip, 0x0, 0x6c);
		output->type = POEMGR_METRIC_STRING;
		output->name = "Allocation mode";
		ret = (ret & 0b1100) >> 2;
		switch (ret) {
		case 0:
			snprintf(output->val_char, sizeof(output->val_char), "Host Defined Power");
			break;
		case 1:
				snprintf(output->val_char, sizeof(output->val_char), "Class Power");
			break;
			case 2:
			snprintf(output->val_char, sizeof(output->val_char), "Highest Possible Power");
			break;
		case 3:
			snprintf(output->val_char, sizeof(output->val_char), "!!Unknown!!");
			break;
		}

	} else if (metric == 5) {
		msb = ip8008_paged_read(pse_chip, 0x0, 0x8e);
		lsb = ip8008_paged_read(pse_chip, 0x0, 0x8f);
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Supply Voltage";
		output->val_uint32 = parse_nbit_value_munit(msb, lsb, 4);

	} else if (metric == 6) {
		msb = ip8008_paged_read(pse_chip, 0x1, 0xa0);
		lsb = ip8008_paged_read(pse_chip, 0x1, 0xa1);
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Supply voltage upper limit";
		output->val_uint32 = parse_nbit_value_munit(msb, lsb, 4);

	} else if (metric == 7) {
		msb = ip8008_paged_read(pse_chip, 0x1, 0xa2);
		lsb = ip8008_paged_read(pse_chip, 0x1, 0xa3);
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Supply voltage lower limit";
		output->val_uint32 = parse_nbit_value_munit(msb, lsb, 4);

	} else if (metric == 8) {
		ret = ip8008_paged_read(pse_chip, 0x0, 0xde);
		output->type = POEMGR_METRIC_STRING;
		output->name = "Limit Type";
		snprintf(output->val_char, sizeof(output->val_char), "%s %s", (!!(ret & 0b10)) ? "Current" : "Power", (ret & 0b1) ? "Enabled" : "Disabled" );

	} else if (metric == 9) {
		ret = ip8008_paged_read(pse_chip, 0x0, 0xdf);
		output->type = POEMGR_METRIC_STRING;
		output->name = "Victim Strategy";
		switch(ret & 0b111) {
		case 0:
			snprintf(output->val_char, sizeof(output->val_char), "Last port powered");
			break;
		case 1:
			snprintf(output->val_char, sizeof(output->val_char), "First port powered");
			break;
		case 2:
			snprintf(output->val_char, sizeof(output->val_char), "Port with Lowest current");
			break;
		case 3:
			snprintf(output->val_char, sizeof(output->val_char), "Port with Highest current");
			break;
		case 4:
			snprintf(output->val_char, sizeof(output->val_char), "Priority");
			break;
		case 5:
		case 6:
		case 7:
			snprintf(output->val_char, sizeof(output->val_char), "!!Unknown!!");
			break;
		}

	} else if (metric == 10) {
		ret = ip8008_paged_read(pse_chip, IP8008_PAGE_POWER_REGISTER, IP8008_REG_POWER_REGISTER);

		msb = ip8008_paged_read(pse_chip, IP8008_PAGE_AVAILABLE_CURRENT, IP8008_REG_AVAILABLE_CURRENT);
		lsb = ip8008_paged_read(pse_chip, IP8008_PAGE_AVAILABLE_CURRENT, IP8008_REG_AVAILABLE_CURRENT + 1);
		output->type = POEMGR_METRIC_UINT32;
		if (!!(ret & 0b10)) {
			output->name = "Available Current";
			output->val_uint32 = (msb << 8) | lsb;
		}
		else {
			output->name = "Available Power";
			int msb_c = ip8008_paged_read(pse_chip, 0x0, 0xd4);
			int lsb_c = ip8008_paged_read(pse_chip, 0x0, 0xd5);
			output->val_uint32 = parse_nbit_value_munit(msb, lsb, 2) - (parse_nbit_value_munit(msb_c, lsb_c, 4));
		}
	} else if (metric == 11) {
		msb = ip8008_paged_read(pse_chip, 0x0, 0xd2);
		lsb = ip8008_paged_read(pse_chip, 0x0, 0xd3);
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Consumed Current";
		output->val_uint32 = parse_nbit_value_munit(msb, lsb, 2) / 1000;

	} else if (metric == 12) {
		msb = ip8008_paged_read(pse_chip, 0x0, 0xd4);
		lsb = ip8008_paged_read(pse_chip, 0x0, 0xd5);
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Consumed Power";
		output->val_uint32 = parse_nbit_value_munit(msb, lsb, 4);

	}
	ret = 0;

	return ret;
}

int ip8008_init(struct poemgr_pse_chip *pse_chip, int i2c_bus, int i2c_addr, uint32_t port_mask)
{
	struct ip8008_priv *priv;
	char i2cpath[30];
	int fd;

	priv = malloc(sizeof(struct ip8008_priv));
	if (!priv)
		return 1;

	priv->i2c_addr = i2c_addr;

	snprintf(i2cpath, sizeof(i2cpath) - 1, "/dev/i2c-%d", i2c_bus);
	fd = open(i2cpath, O_RDWR);

	if (fd == -1) {
		perror(i2cpath);
		goto out_free_priv;
	}

	if (ioctl(fd, I2C_SLAVE, priv->i2c_addr) < 0) {
		perror("i2c_set_address");
		goto out_free_fd;
	}

	priv->i2c_fd = fd;

	pse_chip->priv = (void*) priv;
	pse_chip->portmask = port_mask;
	pse_chip->model = "ip8008";
	pse_chip->num_metrics = 14;
	pse_chip->export_metric = &ip8008_export_metric;

	return 0;

out_free_fd:
	close(fd);
out_free_priv:
	free(priv);

	return 1;
}
