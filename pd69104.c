/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdio.h>
#include <stdlib.h>
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

static struct pd69104_priv *pd69104_priv(struct poemgr_pse_chip *pse_chip) {
	return (struct pd69104_priv *) pse_chip->priv;
}

static int32_t i2c_smbus_access(int file, char read_write, uint8_t command,
				int size, union i2c_smbus_data *data)
{
	struct i2c_smbus_ioctl_data args;

	args.read_write = read_write;
	args.command = command;
	args.size = size;
	args.data = data;
	return ioctl(file,I2C_SMBUS,&args);
}


static int pd69104_wr(struct poemgr_pse_chip *pse_chip, uint8_t reg, uint8_t val)
{
	struct pd69104_priv *priv = pd69104_priv(pse_chip);
	union i2c_smbus_data data;

	data.byte = val;

	return i2c_smbus_access(priv->i2c_fd, I2C_SMBUS_WRITE, reg, 2, &data);
}

static int pd69104_rr(struct poemgr_pse_chip *pse_chip, uint8_t reg)
{
	struct pd69104_priv *priv = pd69104_priv(pse_chip);
	union i2c_smbus_data data;

	if (i2c_smbus_access(priv->i2c_fd, I2C_SMBUS_READ, reg, 2, &data))
		return -1;
	
	return 0x0FF & data.byte;
}

int pd69104_device_online(struct poemgr_pse_chip *pse_chip)
{
	int id_reg = pd69104_rr(pse_chip, PD69104_REG_ID);

	if (id_reg < 0)
		return 0;

	if (((id_reg & PD69104_REG_ID_DEV_MASK) >> PD69104_REG_ID_DEV_SHIFT) != 0x5)
		return 0;
	
	return 1;
}

int pd69104_port_power_consumption_get(struct poemgr_pse_chip *pse_chip, int port)
{
	return pd69104_rr(pse_chip, PD69104_REG_PORT_CONS(port));
}

int pd69104_pwrgd_pin_status_get(struct poemgr_pse_chip *pse_chip)
{
	return (pd69104_rr(pse_chip, PD69104_REG_PWRGD) & PD69104_REG_PWRGD_PIN_STATUS_MASK) >> PD69104_REG_PWRGD_PIN_STATUS_SHIFT;
}

int pd69104_port_operation_mode_get(struct poemgr_pse_chip *pse_chip, int port)
{
	int opmd_reg = pd69104_rr(pse_chip, PD69104_REG_OPMD);
	int opmd_port = (PD69104_REG_OPMD_PORT_MASK(port) & opmd_reg) >> PD69104_REG_OPMD_PORT_SHIFT(port);
	if (opmd_reg < 0)
		return opmd_reg;

	return opmd_port;
}

int pd69104_port_operation_mode_set(struct poemgr_pse_chip *pse_chip, int port, int opmode)
{
	int opmd_reg = pd69104_rr(pse_chip, PD69104_REG_OPMD);

	if (opmd_reg < 0)
		return opmd_reg;

	opmd_reg &= ~PD69104_REG_OPMD_PORT_MASK(port);
	opmd_reg |= opmode << PD69104_REG_OPMD_PORT_SHIFT(port);

	return pd69104_wr(pse_chip, PD69104_REG_OPMD, opmd_reg);
}

int pd69104_port_detection_classification_set(struct poemgr_pse_chip *pse_chip, int port, int enable)
{
	int detena_reg = pd69104_rr(pse_chip, PD69104_REG_DETENA);

	if (detena_reg < 0)
		return detena_reg;

	detena_reg &= ~PD69104_REG_DETENA_DETECTION_PORT_MASK(port);
	detena_reg |= !!enable << PD69104_REG_DETENA_DETECTION_PORT_SHIFT(port);

	detena_reg &= ~PD69104_REG_DETENA_CLASSIFICATION_PORT_MASK(port);
	detena_reg |= !!enable << PD69104_REG_DETENA_CLASSIFICATION_PORT_SHIFT(port);

	return pd69104_wr(pse_chip, PD69104_REG_DETENA, detena_reg);
}

int pd69104_port_poe_class_get(struct poemgr_pse_chip *pse_chip, int port)
{
	int statp = pd69104_rr(pse_chip, PD69104_REG_STATP(port));
	int classification = (statp & PD69104_REG_STATP_CLASSIFICATION_MASK) >> PD69104_REG_STATP_CLASSIFICATION_SHIFT;

	if (statp < 0)
		return statp;

	/**
	 * 0 = Unknown
	 * 1 = Class 1
	 * 2 = Class 2
	 * 3 = Class 3
	 * 4 = Class 4
	 * 5 = Reserved
	 * 6 = Class 0
	 * 7 = Over-current
	 */

	if (classification > 0 && classification < 5)
		return classification;

	if (classification == 6)
		return 0;

	return -1;
}

int pd69104_port_power_enabled_get(struct poemgr_pse_chip *pse_chip, int port)
{
	return !!(PD69104_REG_STATPWR_PWR_ENABLED_PORT_MASK(port) & pd69104_rr(pse_chip, PD69104_REG_STATPWR));
}

int pd69104_port_power_good_get(struct poemgr_pse_chip *pse_chip, int port)
{
	return !!(PD69104_REG_STATPWR_PWR_GOOD_PORT_MASK(port) & pd69104_rr(pse_chip, PD69104_REG_STATPWR));
}

int pd69104_port_power_limit_get(struct poemgr_pse_chip *pse_chip, int port)
{
	return PD69104_REG_PWR_CR_PAL_MASK & pd69104_rr(pse_chip, PD69104_REG_PWR_CR(port));
}

int pd69104_port_power_limit_set(struct poemgr_pse_chip *pse_chip, int port, int val)
{
	return pd69104_wr(pse_chip, PD69104_REG_PWR_CR(port), PD69104_REG_PWR_CR_PAL_MASK & val);
}

int pd69104_system_power_budget_get(struct poemgr_pse_chip *pse_chip, int bank)
{
	return pd69104_rr(pse_chip, PD69104_REG_PWR_BNK(bank));
}

int pd69104_system_power_budget_set(struct poemgr_pse_chip *pse_chip, int bank, int val)
{
	return pd69104_wr(pse_chip, PD69104_REG_PWR_BNK(bank), val);
}

static int pd69104_vtemp_get(struct poemgr_pse_chip *pse_chip)
{
	return pd69104_rr(pse_chip, PD69104_REG_VTEMP);
}

int pd69104_port_faults_get(struct poemgr_pse_chip *pse_chip, int port)
{

	int psr = (pd69104_rr(pse_chip, PD69104_REG_PORT_SR(port)) & PD69104_REG_PORT_SR_MASK(port)) >> PD69104_REG_PORT_SR_SHIFT(port);
	int statp = pd69104_rr(pse_chip, PD69104_REG_STATP(port));
	int detection_result = (statp & PD69104_REG_STATP_DETECTION_MASK) >> PD69104_REG_STATP_DETECTION_SHIFT;
	int classification_result = (statp & PD69104_REG_STATP_CLASSIFICATION_MASK) >> PD69104_REG_STATP_CLASSIFICATION_SHIFT;
	int faults = 0;

	if (psr < 0 || statp < 0)
		return -1;

	switch (detection_result) {
		case PD69104_REG_STATP_DETECTION_SHORT_CIRCUIT:
			faults |= POEMGR_FAULT_TYPE_SHORT_CIRCUIT;
			break;
		case PD69104_REG_STATP_DETECTION_CPD_TOO_HIGH:
			faults |= POEMGR_FAULT_TYPE_CAPACITY_TOO_HIGH;
			break;
		case PD69104_REG_STATP_DETECTION_RSIG_TOO_LOW:
			faults |= POEMGR_FAULT_TYPE_RESISTANCE_TOO_LOW;
			break;
		case PD69104_REG_STATP_DETECTION_RSIG_TOO_HIGH:
			faults |= POEMGR_FAULT_TYPE_RESISTANCE_TOO_HIGH;
			break;		
		case PD69104_REG_STATP_DETECTION_RSIG_OPEN_CIRCUIT:
			faults |= POEMGR_FAULT_TYPE_OPEN_CIRCUIT;
			break;
		default:
			break;
	}

	switch (classification_result) {
		case PD69104_REG_STATP_CLASSIFICATION_OVER_CURRENT:
			faults |= POEMGR_FAULT_TYPE_OVER_CURRENT;
			break;
		default:
			break;
	}

	if (psr & PD69104_REG_PORT_SR_OVER_TEMP) {
		faults |= POEMGR_FAULT_TYPE_OVER_TEMPERATURE;
	}

	if (psr & PD69104_REG_PORT_SR_OFF_PM) {
		faults |= POEMGR_FAULT_TYPE_POWER_MANAGEMENT;
	}

	return faults;
}

int pd69104_export_metric(struct poemgr_pse_chip *pse_chip, struct poemgr_metric *output, int metric)
{
	if (metric < 0 || metric >= pse_chip->num_metrics)
		return -1;

	if (metric == 0) {
		output->type = POEMGR_METRIC_INT32;
		output->name = "temperature";
		output->val_int32 = ((int) pd69104_vtemp_get(pse_chip) * 0.96) - 27;
	}

	return 0;
}

int pd69104_init(struct poemgr_pse_chip *pse_chip, int i2c_bus, int i2c_addr, uint32_t port_mask)
{
	struct pd69104_priv *priv;
	char i2cpath[30];
	int fd;

	priv = malloc(sizeof(struct pd69104_priv));
	if (!priv)
		return 1;

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

	pse_chip->priv = (void *) priv;
	pse_chip->portmask = port_mask;
	pse_chip->model = "PD69104";
	pse_chip->num_metrics = 1;
	pse_chip->export_metric = &pd69104_export_metric;

	return 0;
}

int pd69104_end(struct poemgr_pse_chip *pse_chip)
{
	struct pd69104_priv *priv = pd69104_priv(pse_chip);
	int ret = !!close(priv->i2c_fd);

	free(priv);
	return ret;
}
