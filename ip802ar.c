/* SPDX-License-Identifier: GPL-2.0-only */

#include "ip802ar.h"

#include <fcntl.h>
#include <inttypes.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "common.h"

#define I2C_SMBUS_READ 1
#define I2C_SMBUS_WRITE 0

static struct ip802ar_priv *ip802ar_priv(struct poemgr_pse_chip *pse_chip)
{
	return (struct ip802ar_priv *)pse_chip->priv;
}

static int ip802ar_wr(struct poemgr_pse_chip *pse_chip, uint8_t reg,
		      uint8_t val)
{
	struct ip802ar_priv *priv = ip802ar_priv(pse_chip);

	return i2c_write(priv->i2c_fd, priv->i2c_addr, reg, val);
}

static int ip802ar_nrr(struct poemgr_pse_chip *pse_chip, uint8_t reg, size_t n,
		       void *val)
{
	struct ip802ar_priv *priv = ip802ar_priv(pse_chip);

	return i2c_read_nbytes(priv->i2c_fd, priv->i2c_addr, reg, n, val);
}

static int ip802ar_rr(struct poemgr_pse_chip *pse_chip, uint8_t reg)
{
	uint8_t result = 0;
	int ret;

	ret = ip802ar_nrr(pse_chip, reg, 1, &result);

	if (ret < 0)
		return ret;

	return result;
}

static int ip802ar_masked_wr(struct poemgr_pse_chip *pse_chip, uint8_t reg,
			     uint8_t mask, uint8_t newval)
{
	int ret = -1, val;

	val = ip802ar_rr(pse_chip, reg);
	if (val < 0)
		goto out;

	val &= (~mask);
	val |= newval;

	ret = ip802ar_wr(pse_chip, reg, val);
out:
	return ret;
}

static inline int ip802ar_switch_page(struct poemgr_pse_chip *pse_chip,
				      uint8_t page)
{
	/* valid page value is either 0, 1 or 2 */
	page &= 0b11;

	return ip802ar_masked_wr(pse_chip, IP802AR_REG_SETPAGE,
				 IP802AR_MASK_SETPAGE,
				 (page << IP802AR_OFFSET_SETPAGE));
}

static int ip802ar_paged_burst_read(struct poemgr_pse_chip *pse_chip,
				    uint8_t page, uint8_t reg, size_t n,
				    void *val)
{
	int ret;

	ret = ip802ar_switch_page(pse_chip, page);
	if (ret < 0)
		goto out;

	ret = ip802ar_nrr(pse_chip, reg, n, val);

out:
	return ret;
}

static int ip802ar_paged_read(struct poemgr_pse_chip *pse_chip, uint8_t page,
			      uint8_t reg)
{
	int ret;
	uint8_t val = 0;

	ret = ip802ar_paged_burst_read(pse_chip, page, reg, 1, &val);
	if (ret < 0)
		goto out;

	ret = val;

out:
	return ret;
}

static int ip802ar_paged_write(struct poemgr_pse_chip *pse_chip, uint8_t page,
			       uint8_t reg, uint8_t val)
{
	int ret;

	ret = ip802ar_switch_page(pse_chip, page);
	if (ret < 0)
		goto out;

	ret = ip802ar_wr(pse_chip, reg, val);
	if (ret < 0)
		goto out;

	ret = 0;
out:
	return ret;
}

static int ip802ar_masked_paged_write(struct poemgr_pse_chip *pse_chip,
				      uint8_t page, uint8_t reg, uint8_t mask,
				      uint8_t newval)
{
	int ret = ip802ar_switch_page(pse_chip, page);
	if (ret < 0)
		goto out;

	ret = ip802ar_masked_wr(pse_chip, reg, mask, newval);
	if (ret < 0)
		goto out;

out:
	return ret;
}

static int ip802ar_wait_for_poll(struct poemgr_pse_chip *pse_chip, int retries)
{
	int ret = 0;

	// Force poll on all ports before waiting
	ip802ar_paged_write(pse_chip, IP802AR_PAGE_FORCE_POLL,
			    IP802AR_REG_FORCE_POLL, 0xff);
	for (; retries > 0; retries--) {
		ret = IP802AR_BIT_POLL_IN_PROGRESS &
		      ip802ar_paged_read(pse_chip, IP802AR_PAGE_IVT_POLL,
					 IP802AR_REG_IVT_POLL);

		if (!ret)
			return 0;

		// 1 ms sleep
		usleep(1 * 1000);
	}

	return -1;
}

int ip802ar_set_operation_mode(struct poemgr_pse_chip *pse_chip,
			       enum operation_mode op_mode)
{
	/* page 1 reg 0x01: operation mode */
	return ip802ar_masked_paged_write(pse_chip, IP802AR_PAGE_SYS_CONF,
					  IP802AR_REG_SYS_CONF,
					  IP802AR_MASK_OPMODE, op_mode);
}

int ip802ar_get_operation_mode(struct poemgr_pse_chip *pse_chip)
{
	return ip802ar_paged_read(pse_chip, IP802AR_PAGE_SYS_CONF,
				  IP802AR_REG_SYS_CONF) &
	       IP802AR_MASK_OPMODE;
}

int32_t ip802ar_get_supply_mvolt(struct poemgr_pse_chip *pse_chip)
{
	uint8_t v[2];
	int32_t ret;

	ret = ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);

#if defined(ENABLE_DEBUG)
	if (ret < 0) {
		debug("Waiting for poll exceeded retry limit, continuing anyway\n");
	}
#endif
	ret = ip802ar_paged_burst_read(pse_chip, IP802AR_PAGE_SUPPLY_VOLTAGE,
				       IP802AR_REG_SUPPLY_VOLTAGE, 2, &v[0]);
	if (ret < 0)
		goto out;

	ret = parse_nbit_value_munit(v[0], v[1], IP802AR_FBITS_SUPPLY_VOLTAGE);
out:
	return ret;
}

int32_t ip802ar_get_port_mvolt(struct poemgr_pse_chip *pse_chip, int port)
{
	uint8_t v[2];
	int32_t ret;

	ret = ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);

#if defined(ENABLE_DEBUG)
	if (ret < 0) {
		debug("Waiting for poll exceeded retry limit, continuing anyway\n");
	}
#endif

	ret = ip802ar_paged_burst_read(pse_chip, IP802AR_PAGE_PORT_VOLTAGE,
				       IP802AR_REG_PORT_VOLTAGE(port), 2,
				       &v[0]);
	if (ret < 0)
		goto out;

	ret = parse_nbit_value_munit(v[0], v[1], IP802AR_FBITS_PORT_VOLTAGE);
out:
	return ret;
}

int32_t ip802ar_get_port_mamp(struct poemgr_pse_chip *pse_chip, int port)
{
	uint8_t v[2];
	int32_t ret;

	ret = ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);

#if defined(ENABLE_DEBUG)
	if (ret < 0) {
		debug("Waiting for poll exceeded retry limit, continuing anyway\n");
	}
#endif

	ret = ip802ar_paged_burst_read(pse_chip, IP802AR_PAGE_PORT_CURR,
				       IP802AR_REG_PORT_CURR(port), 2, &v[0]);

	if (ret < 0)
		goto out;

	ret = parse_nbit_value_munit(v[0], v[1], IP802AR_FBITS_PORT_CURR) /
	      1000;
out:
	return ret;
}

int32_t ip802ar_get_port_mcelsius(struct poemgr_pse_chip *pse_chip, int port)
{
	uint8_t v[2];
	int32_t ret;

	ret = ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);

#if defined(ENABLE_DEBUG)
	if (ret < 0) {
		debug("Waiting for poll exceeded retry limit, continuing anyway\n");
	}
#endif

	ret = ip802ar_paged_burst_read(pse_chip, IP802AR_PAGE_PORT_TEMP,
				       IP802AR_REG_PORT_TEMP(port), 2, &v[0]);

	if (ret < 0)
		goto out;

	ret = parse_nbit_value_munit(v[0], v[1], IP802AR_FBITS_PORT_TEMP);
out:
	return ret;
}

int32_t ip802ar_port_power_consumption_get(struct poemgr_pse_chip *pse_chip,
					   int port)
{
	int64_t supply_mvolt, port_mvolt, port_mamp;

	int64_t port_mwatt;

	if (0 > (supply_mvolt = ip802ar_get_supply_mvolt(pse_chip)))
		return supply_mvolt;

	if (0 > (port_mvolt = ip802ar_get_port_mvolt(pse_chip, port)))
		return port_mvolt;

	if (0 > (port_mamp = ip802ar_get_port_mamp(pse_chip, port)))
		return port_mamp;

#if defined(ENABLE_DEBUG)
	debug("supply_mvolt: %"PRId64"\n", supply_mvolt);
	debug("port_mvolt: %"PRId64"\n", port_mvolt);
	debug("port_mamp: %"PRId64"\n", port_mamp);
#endif

	/**
	 * Try to perform division in the end
	 * in the worst case, max mvolt == 256500 mvolt
	 * max uamp == 1023300 uamp
	 */

	/* mwatt = mvolt * mamp = mvolt * uamp / 1000 */
	port_mwatt = (supply_mvolt - port_mvolt) * port_mamp;
	port_mwatt /= 1000;

	// safely return to int32
	if (port_mwatt > INT32_MAX)
		return -1;
	return (int32_t)port_mwatt;
}

int32_t ip802ar_port_good_get(struct poemgr_pse_chip *pse_chip, int port)
{
	int ret;

#if defined(ENABLE_DEBUG)
	ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_SM_CTL,
				 IP802AR_REG_SM_CTL(port));

	debug("REG_SM_CTL(%d): 0x%02x, sm_state: %lu\n", port, ret,
	      ret & IP802AR_MASK_SM_STATE);
#endif

	ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_FAULTS,
				 IP802AR_REG_POWER_DENIED);

#if defined(ENABLE_DEBUG)
	debug("power denied [0x%02x]: %d: %d\n", ret, port,
	      !!(ret & BIT(port)));
#endif

	if (ret & BIT(port)) {
		// power was denied to this port, and it is not on.
		return !!(ret & BIT(port));
	}

	// power was not denied, rely on regular power status register
	return BIT(port) & ip802ar_paged_read(pse_chip,
					      IP802AR_PAGE_PORT_PWR_STS,
					      IP802AR_REG_PORT_PWR_STS);
}

int ip802ar_port_power_limit_get(struct poemgr_pse_chip *pse_chip, int port)
{
	return 0;
}

int ip802ar_port_faults_get(struct poemgr_pse_chip *pse_chip, int port)
{
	int ret = 0;
	int status = -1;

	status = ip802ar_paged_read(pse_chip, IP802AR_PAGE_PORT_EVENT,
				    IP802AR_REG_PORT_EVENT(port));
	if (status < 0)
		goto out;

	if (status == 0)
		goto out;

	if (status & (BIT(0) | BIT(5)))
		ret |= POEMGR_FAULT_TYPE_OVER_CURRENT;
	if (status & BIT(1))
		ret |= POEMGR_FAULT_TYPE_SHORT_CIRCUIT;
	if (status & (BIT(4) | BIT(7)))
		ret |= POEMGR_FAULT_TYPE_OVER_TEMPERATURE;
	if (status & (BIT(3) | BIT(6)))
		ret |= POEMGR_FAULT_TYPE_POWER_MANAGEMENT;

	status = ip802ar_paged_read(pse_chip, IP802AR_PAGE_FAULTS,
				    IP802AR_REG_SHORT_EVT);

	if (status & BIT(port))
		ret |= POEMGR_FAULT_TYPE_SHORT_CIRCUIT;

	status = ip802ar_paged_read(pse_chip, IP802AR_PAGE_FAULTS,
				    IP802AR_REG_CURR_LIM_PWR_OFF_EVT);

	if (status & BIT(port))
		ret |= POEMGR_FAULT_TYPE_OVER_CURRENT;

	status = ip802ar_paged_read(pse_chip, IP802AR_PAGE_FAULTS,
				    IP802AR_REG_POWER_DENIED);

	if (status & BIT(port))
		ret |= POEMGR_FAULT_TYPE_POWER_MANAGEMENT;

	status = ip802ar_paged_read(pse_chip, IP802AR_PAGE_FAULTS,
				    IP802AR_REG_INVALID_SIG_EVT);

	if (status & BIT(port))
		ret |= POEMGR_FAULT_TYPE_CLASSIFICATION_ERROR;

	status = ip802ar_paged_read(pse_chip, IP802AR_PAGE_PORT_DET,
				    IP802AR_REG_PORT_DET(port)) &
		 IP802AR_MASK_PORT_DET;
	if (status < 0)
		goto out;

	switch (status) {
	case IP802AR_PORT_DET_RBAD:
		/* Shows up when nothing is connected */
		break;
	case IP802AR_PORT_DET_RGOOD:
		/* Means nothing is wrong with resistance */
		break;
	case IP802AR_PORT_DET_ROPEN:
		// Clear all other faults, there is nothing connected on the port
		ret = 0;
		break;
	case IP802AR_PORT_DET_CLARGE:
		ret |= POEMGR_FAULT_TYPE_CAPACITY_TOO_HIGH;
		break;
	case IP802AR_PORT_DET_RLOW:
		ret |= POEMGR_FAULT_TYPE_RESISTANCE_TOO_LOW;
		break;
	case IP802AR_PORT_DET_RHIGH:
		ret |= POEMGR_FAULT_TYPE_RESISTANCE_TOO_HIGH;
		break;
	default:
		ret |= POEMGR_FAULT_TYPE_UNKNOWN;
		break;
	}

	/**
	 * TODO: Can be checked against, port good bit to filter out actual error
	 * MPS error can frequently occur when there is nothing connected to PoE port,
	 *
	 */
	// if (status & BIT(2))
	// ret |= POEMGR_FAULT_TYPE_UNKNOWN;

out:
	return ret;
}

int ip802ar_clear_faults(struct poemgr_pse_chip *pse_chip, int num_ports)
{
	int ret = -1;

	for (int i = 0; i < num_ports; i++) {
		ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_PORT_EVENT,
					 IP802AR_REG_PORT_EVENT(i));

		/*
		 * Write same value as what we read, to clear the
		 * faults, we don't care if the write was successful
		 * or not
		 */
		ip802ar_paged_write(pse_chip, IP802AR_PAGE_PORT_EVENT,
				    IP802AR_REG_PORT_EVENT(i), ret);

		ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_FAULTS,
					 IP802AR_REG_SHORT_EVT);
		ip802ar_paged_write(pse_chip, IP802AR_PAGE_FAULTS,
				    IP802AR_REG_SHORT_EVT, ret);

		ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_FAULTS,
					 IP802AR_REG_CURR_LIM_PWR_OFF_EVT);
		ip802ar_paged_write(pse_chip, IP802AR_PAGE_FAULTS,
				    IP802AR_REG_CURR_LIM_PWR_OFF_EVT, ret);

		ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_FAULTS,
					 IP802AR_REG_INVALID_SIG_EVT);
		ip802ar_paged_write(pse_chip, IP802AR_PAGE_FAULTS,
				    IP802AR_REG_INVALID_SIG_EVT, ret);

		ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_FAULTS,
					 IP802AR_REG_POWER_DENIED);
		ip802ar_paged_write(pse_chip, IP802AR_PAGE_FAULTS,
				    IP802AR_REG_POWER_DENIED, ret);
	}

#if defined(ENABLE_DEBUG)
	debug("Power event handle resgister: 0x81: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0x81));
	debug("Power event resgister: 0x70: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0x70));
	debug("Power event resgister: 0x71: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0x71));
	debug("Power event mask resgister: 0x78: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0x78));
	debug("port interrupt register: 0x80: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0x80));
	debug("port power status register: 0x82: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0x82));
	debug("port MPS status register: 0x83: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0x83));
	debug("port voltage bad event register: 0x86: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0x86));
	debug("severe short circuit event register: 0x87: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0x87));
	debug("Total current limiter power off event register: 0x60: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0x60));
	debug("port 0-3 invalid signature event registers: 0xdc: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0xdc));
	debug("port 0-3 power denied event registers: 0xda: %02x\n",
	      ip802ar_paged_read(pse_chip, 1, 0xda));

	if (num_ports)
		goto out;

	ret = 0;
out:
#endif
	return ret;
}

int ip802ar_port_poe_class_get(struct poemgr_pse_chip *pse_chip, int port)
{
	int ret;
	ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_PORT_PD_CLASS,
				 IP802AR_REG_PORT_PD_CLASS(port));
	if (ret < 0)
		goto out;

	ret &= IP802AR_MASK_PORT_PD_CLASS(port);
	ret >>= IP802AR_SHIFT_PORT_PD_CLASS(port);

	// value 5 corresponds to invalid/classification failed
	if (ret > 4)
		ret = -1;

out:
	return ret;
}

int ip802ar_device_online(struct poemgr_pse_chip *pse_chip)
{
	int ret = -1;
	int msb, lsb;

	/* Check chip revision */
	msb = ip802ar_paged_read(pse_chip, IP802AR_PAGE_HW_REVISION,
				 IP802AR_REG_HW_REVISION_MSB);
	lsb = ip802ar_paged_read(pse_chip, IP802AR_PAGE_HW_REVISION,
				 IP802AR_REG_HW_REVISION_LSB);
#if defined(ENABLE_DEBUG)
	debug("msb,lsb: 0x%02x%02x\n", msb, lsb);
#endif
	if (msb != 0x04)
		goto out;
	if (lsb != 0xa2)
		goto out;

	ret = 0;
out:
	return ret;
}

int ip802ar_device_enable_set(struct poemgr_pse_chip *pse_chip, int is_enabled)
{
	int ret;
	/**
	 * 0. check if device online
	 * 1. reset chip
	 * 2. enable manual mode
	 * 3. enable auto polling
	 * 4. set to class based power limits
	 * 5. Enable port power/current/voltage limit event handle
	 * ----- that is it for this version ---
	 * TODO:
	 * 5. set power to at mode
	 */
	ip802ar_device_online(pse_chip);
	ip802ar_set_operation_mode(pse_chip, MANUAL_MODE);
	ip802ar_masked_paged_write(
		pse_chip, IP802AR_PAGE_IVT_POLL, IP802AR_REG_IVT_POLL,
		IP802AR_BIT_AUTO_POLL | IP802AR_MASK_IVT_POLL_PERIOD,
		is_enabled ? IP802AR_BIT_AUTO_POLL | IP802AR_IVT_POLL_PERIOD :
			     0);
	ip802ar_masked_paged_write(pse_chip, IP802AR_PAGE_POWER_CONFIG_MODE,
				   IP802AR_REG_POWER_CONFIG_MODE,
				   IP802AR_MASK_POWER_CONFIG_MODE | IP802AR_MASK_POWER_ESTIMATE_MODE,
				   IP802AR_CLASS_DEFINED_POWER_LIMIT | IP802AR_ESTIMATE_CURRENT_POWER);

	// Enable only power limit
	ip802ar_masked_paged_write(pse_chip, IP802AR_PAGE_LIMIT_CTRL,
				   IP802AR_REG_LIMIT_CTRL,
				   IP802AR_MASK_CURRENT_LIMIT | IP802AR_MASK_POWER_LIMIT |
				   IP802AR_MASK_VICTIM_STRATEGY,
				   IP802AR_MASK_POWER_LIMIT | 0);

	ip802ar_masked_paged_write(pse_chip, IP802AR_PAGE_PORT_PWR_EVT_HDL,
				   IP802AR_REG_PORT_PWR_EVT_HDL,
				   IP802AR_BIT_PORT_TEMP_LMT_EVT | IP802AR_BIT_TRK_VOLT_LMT_EVT |
				   IP802AR_BIT_PORT_CUR_LMT_EVT | IP802AR_BIT_PORT_VOLT_LMT_EVT,
				   IP802AR_BIT_PORT_TEMP_LMT_EVT | IP802AR_BIT_TRK_VOLT_LMT_EVT |
				   IP802AR_BIT_PORT_CUR_LMT_EVT | IP802AR_BIT_PORT_VOLT_LMT_EVT);

#if defined(ENABLE_DEBUG)
	debug("port power event handle register: page:%d, reg:0x%02x: 0x%02x\n",
	      IP802AR_PAGE_PORT_PWR_EVT_HDL, IP802AR_REG_PORT_PWR_EVT_HDL,
	      ip802ar_paged_read(pse_chip, IP802AR_PAGE_PORT_PWR_EVT_HDL,
				 IP802AR_REG_PORT_PWR_EVT_HDL));
#endif

	// Disable all events
	ip802ar_paged_write(pse_chip, IP802AR_PAGE_PWR_EVT, IP802AR_REG_PWR_EVT,
			    0x00);

	// Power off when one of master or slave port is in the DC disconnect situation
	ret = ip802ar_masked_paged_write(pse_chip, IP802AR_PAGE_HIPWR_CTRL,
					 IP802AR_REG_HIPWR_CTRL,
					 IP802AR_BIT_HIPWR_DISCON_MODE, 0);

	// out:
	return ret;
}

static int ip802ar_port_operation_mode_get(struct poemgr_pse_chip *pse_chip,
					   int port)
{
	return ip802ar_paged_read(pse_chip, IP802AR_PAGE_PORT_POWER_CTRL,
				  IP802AR_REG_PORT_POWER_CTRL(port)) &
	       IP802AR_MASK_PORT_POWER_CTRL;
}

static int ip802ar_port_operation_mode_set(struct poemgr_pse_chip *pse_chip,
					   int port, int opmode)
{
	return ip802ar_masked_paged_write(pse_chip,
					  IP802AR_PAGE_PORT_POWER_CTRL,
					  IP802AR_REG_PORT_POWER_CTRL(port),
					  IP802AR_MASK_PORT_POWER_CTRL, opmode);
}

int ip802ar_port_afat_mode_set(struct poemgr_pse_chip *pse_chip, int port,
			       int mode)
{
	return ip802ar_masked_paged_write(pse_chip, IP802AR_PAGE_PORT_AFAT_MODE,
					  IP802AR_REG_PORT_AFAT_MODE,
					  IP802AR_MASK_PORT_AFAT_MODE(port),
					  mode);
}

int ip802ar_port_enable_set(struct poemgr_pse_chip *pse_chip, int port,
			    int enable_disable)
{
	return ip802ar_port_operation_mode_set(
		pse_chip, port,
		enable_disable ? IP802AR_PORT_ENABLED : IP802AR_PORT_DISABLED);
}

int ip802ar_port_enable_get(struct poemgr_pse_chip *pse_chip, int port)
{
	return ip802ar_port_operation_mode_get(pse_chip, port);
}

int ip802ar_system_current_budget_set(struct poemgr_pse_chip *pse_chip,
				      int milliamps)
{
	int ret = -1, msb, lsb;

	// Last 4 bits are fractional
	milliamps <<= 4;

	msb = (milliamps >> 8) & IP802AR_MASK_AVAILABLE_CURRENT_MSB;
	lsb = milliamps & IP802AR_MASK_AVAILABLE_CURRENT_LSB;

#if defined(ENABLE_DEBUG)
	debug("Setting current budget: %d:0x%02x 0x%02x 0x%02x\n",
	      IP802AR_PAGE_CURRENT, IP802AR_REG_AVAILABLE_CURRENT, msb, lsb);
#endif

	ret = ip802ar_masked_paged_write(pse_chip, IP802AR_PAGE_CURRENT,
					 IP802AR_REG_AVAILABLE_CURRENT,
					 IP802AR_MASK_AVAILABLE_CURRENT_MSB,
					 msb);
	if (ret < 0)
		goto out;

	ret = ip802ar_masked_paged_write(pse_chip, IP802AR_PAGE_CURRENT,
					 IP802AR_REG_AVAILABLE_CURRENT + 1,
					 IP802AR_MASK_AVAILABLE_CURRENT_LSB,
					 lsb);
	if (ret < 0)
		goto out;

	ret = 0;
out:
	return ret;
}

int ip802ar_system_power_budget_set(struct poemgr_pse_chip *pse_chip, int bank,
				    int val)
{
	int ret = -1, msb, lsb;
	if (bank != 0 && bank != 1)
		goto out;

	msb = (val >> 8) & IP802AR_MASK_TRUNK_LIMIT_MSB;
	lsb = val & IP802AR_MASK_TRUNK_LIMIT_LSB;

	ret = ip802ar_masked_paged_write(pse_chip, IP802AR_PAGE_TRUNK_LIMIT,
					 IP802AR_REG_TRUNK_LIMIT(bank),
					 IP802AR_MASK_TRUNK_LIMIT_MSB, msb);
	if (ret < 0)
		goto out;

	ret = ip802ar_masked_paged_write(pse_chip, IP802AR_PAGE_TRUNK_LIMIT,
					 IP802AR_REG_TRUNK_LIMIT(bank) + 1,
					 IP802AR_MASK_TRUNK_LIMIT_LSB, lsb);
	if (ret < 0)
		goto out;

	if (val > 0xff) {
		ret = -2;
		debug("Power limit > 255 is not supported.\n");
		goto out;
	}

	ret = ip802ar_paged_write(pse_chip, _IP802AR_PAGE_AVAILABLE_POWER,
				  _IP802AR_REG_AVAILABLE_POWER, val & 0xff);
	if (ret < 0)
		goto out;

	ret = 0;
out:
	return ret;
}

int ip802ar_export_port_metric(struct poemgr_pse_chip *pse_chip, int port,
			       const struct poemgr_port_status *p_status,
			       struct poemgr_metric *output, int metric)
{
	int ret = -1; //, msb, lsb;

	static int previous_port = 0;
	static uint32_t allocated_power = 0;

	if (previous_port != port) {
		allocated_power = 0;
		previous_port = port;
	}

	if (metric < 0)
		return -1;

	if (metric == 0) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Current";
		output->val_uint32 = ip802ar_get_port_mamp(pse_chip, port);

	} else if (metric == 1) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Voltage";
		output->val_uint32 = ip802ar_get_supply_mvolt(pse_chip) -
				     ip802ar_get_port_mvolt(pse_chip, port);

	} else if (metric == 2) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Temperature";
		output->val_uint32 = ip802ar_get_port_mcelsius(pse_chip, port);

	} else if (metric == 3) {
		output->type = POEMGR_METRIC_STRING;
		output->name = "Detected PoE Signature";
		ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_PORT_DET,
					 IP802AR_REG_PORT_DET(port)) &
		      IP802AR_MASK_PORT_DET;
		if (ret < 0)
			goto out;

		switch (ret) {
		case IP802AR_PORT_DET_RBAD:
			snprintf(output->val_char, sizeof(output->val_char),
				 "R Bad");
			break;
		case IP802AR_PORT_DET_RGOOD:
			snprintf(output->val_char, sizeof(output->val_char),
				 "R Good");
			break;
		case IP802AR_PORT_DET_ROPEN:
			snprintf(output->val_char, sizeof(output->val_char),
				 "R Open");
			break;
		case IP802AR_PORT_DET_CLARGE:
			snprintf(output->val_char, sizeof(output->val_char),
				 "C Large");
			break;
		case IP802AR_PORT_DET_RLOW:
			snprintf(output->val_char, sizeof(output->val_char),
				 "R Low");
			break;
		case IP802AR_PORT_DET_RHIGH:
			snprintf(output->val_char, sizeof(output->val_char),
				 "R High");
			break;
		default:
			snprintf(output->val_char, sizeof(output->val_char),
				 "!!Unknown!!");
			break;
		}
	} else if (metric == 4) {
		ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);
		ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_REQUESTED_POWER,
					 IP802AR_REG_REQUESTED_POWER(port));
		if (ret < 0)
			goto out;
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Allocated Power";
		output->val_uint32 = parse_nbit_value_munit(0, ret, 2);
		allocated_power = output->val_uint32;
	} else if (metric == 5) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Available Power";
		output->val_uint32 = allocated_power - p_status->power;
	} else {
		output->type = POEMGR_METRIC_END;
	}

	ret = 0;
out:
	return ret;
}

int ip802ar_export_metric(struct poemgr_pse_chip *pse_chip,
			  struct poemgr_metric *output, int metric)
{
	int ret = -1;
	uint8_t val[2];

	// Persist values accross function calls
	static uint32_t max_power = 0;
	static uint32_t allocated_power = 0;
	static uint32_t consumed_power = 0;

	if (metric < 0 || metric >= pse_chip->num_metrics)
		return -1;

	if (metric == 0) {
		ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_TRUNK,
					 IP802AR_REG_TRUNK_SELECT);
		output->type = POEMGR_METRIC_INT32;
		output->name = "Trunk";
		output->val_int32 = ret;

	} else if (metric == 1) {
		ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);

		ret = ip802ar_paged_burst_read(pse_chip, IP802AR_PAGE_TRUNK,
					       IP802AR_REG_TRUNK0_POWER_LIMIT,
					       2, &val[0]);

		output->type = POEMGR_METRIC_UINT32;
		output->name = "Trunk 0 Power Limit";
		output->val_uint32 = ((val[0] << 8) | val[1]) * 1000;

	} else if (metric == 2) {
		ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);

		ret = ip802ar_paged_burst_read(pse_chip, IP802AR_PAGE_TRUNK,
					       IP802AR_REG_TRUNK1_POWER_LIMIT,
					       2, &val[0]);

		output->type = POEMGR_METRIC_UINT32;
		output->name = "Trunk 1 Power Limit";
		output->val_uint32 = ((val[0] << 8) | val[1]) * 1000;

	} else if (metric == 3) {
		ret = ip802ar_paged_read(pse_chip,
					 IP802AR_PAGE_POWER_CONFIG_MODE,
					 IP802AR_REG_POWER_CONFIG_MODE);
		output->type = POEMGR_METRIC_STRING;
		output->name = "Estimation mode";
		switch (ret & IP802AR_MASK_POWER_ESTIMATE_MODE) {
		case IP802AR_ESTIMATE_CLASS_POWER:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Class Power");
			break;
		case IP802AR_ESTIMATE_CURRENT_POWER:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Real Consumed Power");
			break;
		case IP802AR_ESTIMATE_MAX_POWER:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Max Consumed Power");
			break;
		case 3:
			// FIXME:
			// From other similar PoE Chips...
			snprintf(output->val_char, sizeof(output->val_char),
				 "Real Consumed Power and Auto Class Power");
			break;
		default:
			snprintf(output->val_char, sizeof(output->val_char),
				 "!!Unknown!!");
			break;
		}

	} else if (metric == 4) {
		ret = ip802ar_paged_read(pse_chip,
					 IP802AR_PAGE_POWER_CONFIG_MODE,
					 IP802AR_REG_POWER_CONFIG_MODE);
		output->type = POEMGR_METRIC_STRING;
		output->name = "Allocation mode";
		ret = (ret & IP802AR_MASK_POWER_CONFIG_MODE);
		switch (ret) {
		case IP802AR_HOST_DEFINED_POWER_LIMIT:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Host Defined Power");
			break;
		case IP802AR_CLASS_DEFINED_POWER_LIMIT:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Class Power");
			break;
		case IP802AR_MAX_AFAT_POWER_LIMIT:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Highest Possible Power");
			break;
		default:
			snprintf(output->val_char, sizeof(output->val_char),
				 "!!Unknown!!");
			break;
		}

	} else if (metric == 5) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Supply Voltage";
		output->val_uint32 = ip802ar_get_supply_mvolt(pse_chip);

	} else if (metric == 6) {
		ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_LIMIT_CTRL,
					 IP802AR_REG_LIMIT_CTRL);
		output->type = POEMGR_METRIC_STRING;
		output->name = "Limit Type";

		switch (ret & (IP802AR_MASK_CURRENT_LIMIT |
			       IP802AR_MASK_POWER_LIMIT)) {
		case 0:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Disabled");
			break;
		case IP802AR_MASK_CURRENT_LIMIT:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Current Enabled");
			break;
		case IP802AR_MASK_POWER_LIMIT:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Power Enabled");
			break;
		case IP802AR_MASK_CURRENT_LIMIT | IP802AR_MASK_POWER_LIMIT:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Current+Power Enabled");
			break;
		}

	} else if (metric == 7) {
		ret = ip802ar_paged_read(pse_chip, IP802AR_PAGE_LIMIT_CTRL,
					 IP802AR_REG_LIMIT_CTRL);
		output->type = POEMGR_METRIC_STRING;
		output->name = "Victim Strategy";
		switch (ret & IP802AR_MASK_VICTIM_STRATEGY) {
		case 0:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Last port powered");
			break;
		case 1:
			snprintf(output->val_char, sizeof(output->val_char),
				 "First port powered");
			break;
		case 2:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Port with Lowest current");
			break;
		case 3:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Port with Highest current");
			break;
		case 4:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Port with lowest priority");
			break;
		case 5:
		case 6:
		case 7:
			snprintf(output->val_char, sizeof(output->val_char),
				 "!!Unknown!!");
			break;
		}

	} else if (metric == 8) {
		ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);
		ret = ip802ar_paged_burst_read(pse_chip, IP802AR_PAGE_CURRENT,
					       IP802AR_REG_AVAILABLE_CURRENT, 2,
					       &val[0]);

		output->type = POEMGR_METRIC_UINT32;
		output->name = "Max Current";
		output->val_uint32 =
			parse_nbit_value_munit(val[0], val[1], 4) / 1000;

	} else if (metric == 9) {
		ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);
		output->name = "Max Power";
		output->type = POEMGR_METRIC_UINT32;

#if defined(USE_RO_REG)
		ret = ip802ar_paged_burst_read(pse_chip, IP802AR_PAGE_POWER,
					       IP802AR_REG_AVAILABLE_POWER, 2,
					       &val[0]);
		// milliwatts
		output->val_uint32 = parse_nbit_value_munit(val[0], val[1], 4);
		max_power = output->val_uint32;
#else
		// Ignore fraction
		// milliwatts
		output->val_uint32 =
			ip802ar_paged_read(pse_chip,
					   _IP802AR_PAGE_AVAILABLE_POWER,
					   _IP802AR_REG_AVAILABLE_POWER) *
			1000;
		max_power = output->val_uint32;
#endif /* USE_RO_REG */
	} else if (metric == 10) {
		ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);
		ret = ip802ar_paged_burst_read(pse_chip, IP802AR_PAGE_CURRENT,
					       IP802AR_REG_CONSUMED_CURRENT, 2,
					       &val[0]);

		output->type = POEMGR_METRIC_UINT32;
		output->name = "Consumed Current";
		output->val_uint32 =
			parse_nbit_value_munit(val[0], val[1], 4) / 1000;

	} else if (metric == 11) {
		ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);
		ret = ip802ar_paged_burst_read(pse_chip, IP802AR_PAGE_POWER,
					       IP802AR_REG_CONSUMED_POWER, 2,
					       &val[0]);

		output->type = POEMGR_METRIC_UINT32;
		output->name = "Consumed Power";
		output->val_uint32 = parse_nbit_value_munit(val[0], val[1], 8);
		consumed_power = output->val_uint32;
	} else if (metric == 12) {
		ip802ar_wait_for_poll(pse_chip, IP802AR_POLL_IN_PROGRESS_RETRY);
		ret = ip802ar_paged_burst_read(pse_chip,
					       IP802AR_PAGE_PSE_ALLOCATED_POWER,
					       IP802AR_REG_PSE_ALLOCATED_POWER,
					       2, &val[0]);

		output->type = POEMGR_METRIC_UINT32;
		output->name = "Allocated Power";
		output->val_uint32 = parse_nbit_value_munit(val[0], val[1], 4);
		allocated_power = output->val_uint32;
	} else if (metric == 13) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Available Power";
#if defined(IP802AR_CONSERVATIVE_AVAILABLE_POWER)
		output->val_uint32 = max_power - allocated_power;
#else /* IP802AR_CONSERVATIVE_AVAILABLE_POWER */
		output->val_uint32 = max_power - consumed_power;
#endif /* IP802AR_CONSERVATIVE_AVAILABLE_POWER */

		// Silence variable not used warning
		(void) allocated_power;
		(void) consumed_power;
	} else {
		max_power = 0;
		allocated_power = 0;
		consumed_power = 0;
		output->type = POEMGR_METRIC_END;
	}
	ret = 0;
	return ret;
}

int ip802ar_init(struct poemgr_pse_chip *pse_chip, int i2c_bus, int i2c_addr,
		 uint32_t port_mask)
{
	struct ip802ar_priv *priv;
	char i2cpath[30];
	int fd;

	priv = malloc(sizeof(struct ip802ar_priv));
	if (!priv)
		return 1;

	priv->i2c_addr = i2c_addr;

	snprintf(i2cpath, sizeof(i2cpath) - 1, "/dev/i2c-%d", i2c_bus);

#if defined(ENABLE_DEBUG)
	debug("i2c_bus: %s\n", i2cpath);
#endif

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

	pse_chip->priv = (void *)priv;
	pse_chip->portmask = port_mask;
	pse_chip->model = "IP802AR";
	pse_chip->num_metrics = 14;
	pse_chip->export_metric = &ip802ar_export_metric;

	return 0;

out_free_fd:
	close(fd);
out_free_priv:
	free(priv);

	return 1;
}
