/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include <stdint.h>

#include "poemgr.h"

/* Registers */

#define IP8008_COMBINE_BYTES(msb, lsb) ((msb << 8) | lsb)

/* set page */
#define IP8008_REG_SETPAGE (0x0)
#define IP8008_OFFSET_SETPAGE (6)
#define IP8008_LENGTH_SETPAGE (1)
#define IP8008_MASK_SETPAGE (GENMASK_OL(IP8008_OFFSET_SETPAGE, IP8008_LENGTH_SETPAGE))

/* Device id */
#define IP8008_PAGE_DEVICE_ID (0)
#define IP8008_REG_DEVICE_ID_MSB (0x01)
#define IP8008_REG_DEVICE_ID_LSB (0x02)

/* Reset */
#define IP8008_PAGE_RESET (0)
#define IP8008_REG_RESET (0x04)

/* bt mode register */
#define IP8008_PAGE_BT_MODE (0)
#define IP8008_REG_BT_MODE (0x07)

/* Power control (port) */
#define IP8008_PAGE_POWER_CONTROL (0)
#define IP8008_REG_POWER_CONTROL(port) (0x10 + port)

#define IP8008_OFFSET_PSE_ENABLE (0)
#define IP8008_LENGTH_PSE_ENABLE (2)
#define IP8008_MASK_PSE_ENABLE (GENMASK_OL(IP8008_OFFSET_PSE_ENABLE, IP8008_LENGTH_PSE_ENABLE))
#define IP8008_OFFSET_SUSPENDED_POWER_UP (4)
#define IP8008_LENGTH_SUSPENDED_POWER_UP (1)
#define IP8008_MASK_SUSPENDED_POWER_UP (GENMASK_OL(IP8008_OFFSET_SUSPENDED_POWER_UP, IP8008_LENGTH_SUSPENDED_POWER_UP))

/* State machine control (port) */
#define IP8008_PAGE_SM_CTRL (0)
#define IP8008_REG_SM_CTRL(port) (0x18 + port)

/* detection connection check (port) */
#define IP8008_PAGE_DET_CONN_CHECK (0)
#define IP8008_REG_DET_CONN(port) (0x20 + port)

/* requested class status (port) */
#define IP8008_PAGE_REQ_CLASS_STATUS (0)
#define IP8008_REG_REQ_CLASS_STATUS(port) (0x28 + port)

/* allocated class status (port) */
#define IP8008_PAGE_ALLOC_CLASS_STATUS (0)
#define IP8008_REG_ALLOC_CLASS_STATUS(port) (0x30 + port)

/* Power allocation mode */
#define IP8008_PAGE_POWER_ALLOC_MODE (0)
#define IP8008_REG_POWER_ALLOC_MODE (0x6c)
#define IP8008_OFFSET_POWER_ALLOC_MODE (2)
#define IP8008_LENGTH_POWER_ALLOC_MODE (1)
#define IP8008_MASK_POWER_ALLOC_MODE (GENMASK_OL(IP8008_OFFSET_POWER_ALLOC_MODE, IP8008_LENGTH_POWER_ALLOC_MODE))


/* Current status (port) */
#define IP8008_PAGE_CURRENT_STATUS (0)
#define IP8008_REG_CURRENT_STATUS(port) (0x90 + (port * 2))

/* Voltage status (port) */
#define IP8008_PAGE_VOLTAGE_STATUS (0)
#define IP8008_REG_VOLTAGE_STATUS(port) (0xa0 + (port * 2))

/* temperature status (port) */
#define IP8008_PAGE_TEMP_STATUS (0)
#define IP8008_REG_TEMP_STATUS(port) (0xb0 + (port * 2))

/* power consumption (port) */
#define IP8008_PAGE_CONSUMED_POWER (0)
#define IP8008_REG_CONSUMED_POWER(port) (0xc0 + (port * 2))

/* supply voltage */
#define IP8008_PAGE_SUP_VOLT (0)
#define IP8008_REG_SUP_VOLT (0x8e)

/* status (port) */
#define IP8008_PAGE_STATUS (0)
#define IP8008_REG_STATUS(port) (0x70 + (port * 2))

/* IVT Poll */
#define IP8008_PAGE_IVT_POLL (0)
#define IP8008_REG_IVT_POLL (0x81)
#define IP8008_BIT_AUTO_POLL (BIT(4))
#define IP8008_BIT_IVT_PROCESS (BIT(7))
#define IP8008_MASK_IVT_POLL_INTERVAL (GENMASK(3, 0))

/* port power status */
#define IP8008_PAGE_PORT_POWER_STATUS (0)
#define IP8008_REG_PORT_POWER_STATUS (0x60)

/* auto clear limits */
#define IP8008_PAGE_AUTO_CLEAR (0)
#define IP8008_REG_AUTO_CLEAR (0x6f)

/* power register */
#define IP8008_PAGE_POWER_REGISTER (0)
#define IP8008_REG_POWER_REGISTER (0xde)

/* victim strategy */
#define IP8008_PAGE_VICTIM_STRATEGY (0)
#define IP8008_REG_VICTIM_STRATEGY (0xdf)

/* PSE available current */
#define IP8008_PAGE_AVAILABLE_CURRENT (0)
#define IP8008_REG_AVAILABLE_CURRENT (0xe8)


#define IP8008_PAGE_CURRENT_LIMIT (1)
#define IP8008_REG_CURRENT_LIMIT(port) (0xb0 + (port * 2))

/* End ip8008 register defs */

#define POE_ERRORDELAY_TIME			(100)		/* per 0.1second */


struct ip8008_priv {
	int i2c_fd;
	int i2c_addr;
};


int ip8008_init(struct poemgr_pse_chip *pse_chip, int i2c_bus, int i2c_addr, uint32_t port_mask);
int ip8008_end(struct poemgr_pse_chip *pse_chip);

int ip8008_port_poe_class_get(struct poemgr_pse_chip *pse_chip, int port);
int ip8008_port_faults_get(struct poemgr_pse_chip *pse_chip, int port);

int ip8008_port_clear_faults(struct poemgr_pse_chip *pse_chip, int num_ports);

int ip8008_port_power_limit_get(struct poemgr_pse_chip *pse_chip, int port);
int ip8008_port_good_get(struct poemgr_pse_chip *pse_chip, int port);
int ip8008_port_power_consumption_get(struct poemgr_pse_chip *pse_chip, int port);
int ip8008_power_budget_set(struct poemgr_pse_chip *pse_chip, int current);
int ip8008_port_enable_get(struct poemgr_pse_chip *pse_chip, int port);
int ip8008_port_enable_set(struct poemgr_pse_chip *pse_chip, int port, int enable_disable);
int ip8008_device_enable_set(struct poemgr_pse_chip *pse_chip, int is_enabled);
int ip8008_device_online(struct poemgr_pse_chip *pse_chip);

int ip8008_export_port_metric(struct poemgr_pse_chip *pse_chip, int port, struct poemgr_metric *output, int metric);
