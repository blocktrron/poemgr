/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include <stdint.h>

#include "common.h"
#include "poemgr.h"

/*
 * Calculate available power conservatively,
 * Conservative approach uses class defined power
 * Uncomment following line to output available power based on class defined power.
 *
 */
// #define IP802AR_CONSERVATIVE_AVAILABLE_POWER

#define IP802AR_COMBINE_BYTES(msb, lsb) ((msb << 8) | lsb)
#define IP802AR_POLL_IN_PROGRESS_RETRY (20)

#define IP802AR_REG_SETPAGE (0x00)
#define IP802AR_OFFSET_SETPAGE (6)
#define IP802AR_MASK_SETPAGE GENMASK(7, IP802AR_OFFSET_SETPAGE)

#define IP802AR_PAGE_RESET 1
#define IP802AR_REG_RESET 0x02

#define IP802AR_PAGE_HW_REVISION 1
#define IP802AR_REG_HW_REVISION_MSB 0x03
#define IP802AR_REG_HW_REVISION_LSB 0x04

#define IP802AR_PAGE_FORCE_POLL (0)
#define IP802AR_REG_FORCE_POLL (0xE2)

/* Poll period is in units of 8ms, default value as per datasheet is 2 (16ms period)*/
#define IP802AR_PAGE_IVT_POLL (0)
#define IP802AR_REG_IVT_POLL (0xE3)
#define IP802AR_MASK_IVT_POLL_PERIOD GENMASK(3, 0)
// 12 * 0.25 ms = 3 ms
#define IP802AR_IVT_POLL_PERIOD (12)
#define IP802AR_BIT_AUTO_POLL BIT(5)
#define IP802AR_BIT_POLL_IN_PROGRESS BIT(7)

/* Opmode */
#define IP802AR_PAGE_SYS_CONF 1
#define IP802AR_REG_SYS_CONF 0x01
#define IP802AR_MASK_OPMODE (GENMASK(7, 6))

/* Supply voltage */
#define IP802AR_PAGE_SUPPLY_VOLTAGE 0
#define IP802AR_REG_SUPPLY_VOLTAGE 0xe0
// 4 bits fraction
#define IP802AR_FBITS_SUPPLY_VOLTAGE (4)

/* port voltage */
#define IP802AR_PAGE_PORT_VOLTAGE 0
// 2 registers per port
#define IP802AR_REG_PORT_VOLTAGE(port) (0xb0 + (port * 2))
#define IP802AR_FBITS_PORT_VOLTAGE (4)

/* port temperature */
#define IP802AR_PAGE_PORT_TEMP 0
// 2 registers per port
#define IP802AR_REG_PORT_TEMP(port) (0xc0 + (port * 2))
#define IP802AR_FBITS_PORT_TEMP (4)

/* port current */
#define IP802AR_PAGE_PORT_CURR 0
// 2 registers per port
#define IP802AR_REG_PORT_CURR(port) (0xa0 + (port * 2))
#define IP802AR_FBITS_PORT_CURR (2)

/* port power */
#define IP802AR_PAGE_REQUESTED_POWER (0)
#define IP802AR_REG_REQUESTED_POWER(port) (0x90 + port)

/* port detection */
#define IP802AR_PAGE_PORT_DET 0
#define IP802AR_REG_PORT_DET(port) (0x68 + port)
#define IP802AR_MASK_PORT_DET GENMASK(2, 0)
#define IP802AR_PORT_DET_RBAD (0b000)
#define IP802AR_PORT_DET_RGOOD (0b001)
#define IP802AR_PORT_DET_ROPEN (0b010)
#define IP802AR_PORT_DET_CLARGE (0b100)
#define IP802AR_PORT_DET_RLOW (0b101)
#define IP802AR_PORT_DET_RHIGH (0b110)

/* Power Limit */
#define IP802AR_PWR_TRUNK_NUM 2
#define IP802AR_PAGE_TRUNK_LIMIT 1
#define IP802AR_REG_TRUNK_LIMIT(trunk) (0x40 + (trunk * 2))
#define IP802AR_MASK_TRUNK_LIMIT_MSB GENMASK(2, 0)
#define IP802AR_MASK_TRUNK_LIMIT_LSB GENMASK(7, 0)

#define IP802AR_MASK_TRUNK_LIMIT                            \
	IP802AR_COMBINE_BYTES(IP802AR_MASK_TRUNK_LIMIT_MSB, \
			      IP802AR_MASK_TRUNK_LIMIT_LSB)

/* Port power control */
#define IP802AR_PORT_DISABLED 0b00
#define IP802AR_PORT_ENABLED 0b01
#define IP802AR_PORT_FORCE 0b10
/* Test only mode, not for production */
// #define PORT_ENABLED_SKIP_DETECTION 0b11
#define IP802AR_PAGE_PORT_POWER_CTRL 1
#define IP802AR_REG_PORT_POWER_CTRL(port) (0x98 + port)
#define IP802AR_MASK_PORT_POWER_CTRL GENMASK(1, 0)

/* state machine control */
#define IP802AR_PAGE_SM_CTL (1)
#define IP802AR_REG_SM_CTL(port) (0x90 + port)
// #define IP802AR_BIT_START_PWR BIT(5)
#define IP802AR_MASK_SM_STATE GENMASK(4, 0)

/* Power configuration mode */
#define IP802AR_PAGE_POWER_CONFIG_MODE 1
#define IP802AR_REG_POWER_CONFIG_MODE 0x10
#define IP802AR_MASK_POWER_CONFIG_MODE GENMASK(4, 3)
#define IP802AR_HOST_DEFINED_POWER_LIMIT (0 << 3)
#define IP802AR_CLASS_DEFINED_POWER_LIMIT (1 << 3)
#define IP802AR_MAX_AFAT_POWER_LIMIT (2 << 3)
#define IP802AR_MASK_POWER_ESTIMATE_MODE GENMASK(1, 0)
#define IP802AR_ESTIMATE_CLASS_POWER (0)
#define IP802AR_ESTIMATE_CURRENT_POWER (1)
#define IP802AR_ESTIMATE_MAX_POWER (2)

/* Port AFAT mode */
#define IP802AR_PAGE_PORT_AFAT_MODE 0
#define IP802AR_REG_PORT_AFAT_MODE 0x25
#define IP802AR_MASK_PORT_AFAT_MODE(port) BIT(port)
#define IP802AR_AF_MODE(port) 0
#define IP802AR_AT_MODE(port) BIT(port)

/* Port Detected PD Class */
#define IP802AR_PAGE_PORT_PD_CLASS 0
#define IP802AR_REG_PORT_PD_CLASS(port) (0x88 + (port / 2))

#define IP802AR_SHIFT_PORT_PD_CLASS(port) (4 * (port % 2))
#define IP802AR_MASK_PORT_PD_CLASS(port) \
	(GENMASK(2, 0) << IP802AR_SHIFT_PORT_PD_CLASS(port))

#define IP802AR_PD_CLASS_0 0
#define IP802AR_PD_CLASS_1 1
#define IP802AR_PD_CLASS_2 2
#define IP802AR_PD_CLASS_3 3
#define IP802AR_PD_CLASS_4 4
#define IP802AR_PD_CLASS_UNKNOWN 5

#define IP802AR_PAGE_PORT_PWR_EVT_HDL (1)
#define IP802AR_REG_PORT_PWR_EVT_HDL (0x81)
#define IP802AR_BIT_PORT_TEMP_LMT_EVT BIT(7)
#define IP802AR_BIT_TRK_VOLT_LMT_EVT BIT(6)
#define IP802AR_BIT_PORT_CUR_LMT_EVT BIT(5)
#define IP802AR_BIT_PORT_VOLT_LMT_EVT BIT(3)

#define IP802AR_PAGE_PWR_EVT (1)
#define IP802AR_REG_PWR_EVT (0x78)
#define IP802AR_PAGE_HIPWR_CTRL (1)
#define IP802AR_REG_HIPWR_CTRL (0xF3)
#define IP802AR_BIT_HIPWR_DISCON_MODE BIT(4)

#define IP802AR_PAGE_PORT_PWR_STS (1)
#define IP802AR_REG_PORT_PWR_STS (0x82)

/* Fault events */
#define IP802AR_PAGE_PORT_EVENT (1)
#define IP802AR_REG_PORT_EVENT(port) (0x70 + port)

/* current_overload_event */
#define IP802AR_PAGE_FAULTS (1)
// TODO:what is 0x85
#define IP802AR_REG_CUR_OVERLOAD_EVT (0x84)
#define IP802AR_BIT_CUR_OVERLOAD_EVT BIT(5)
#define IP802AR_REG_BAD_VOLT_EVT (0x86)
#define IP802AR_BIT_BAD_VOLT_EVT BIT(4)
#define IP802AR_REG_SHORT_EVT (0x87)
#define IP802AR_REG_CURR_LIM_PWR_OFF_EVT (0x60)
#define IP802AR_REG_INVALID_SIG_EVT (0xDC)
#define IP802AR_REG_POWER_DENIED (0xDA)

/* Metrics */

/* Trunk selection and limits */
#define IP802AR_PAGE_TRUNK (1)

#define IP802AR_REG_TRUNK_SELECT (0x69)
#define IP802AR_TRUNKX_MSB_BITS (0b111)
#define IP802AR_REG_TRUNK0_POWER_LIMIT (0x40)
#define IP802AR_REG_TRUNK1_POWER_LIMIT (0x42)

/* Current/Power limit type */
#define IP802AR_PAGE_LIMIT_CTRL (1)
#define IP802AR_REG_LIMIT_CTRL (0xc0)
#define IP802AR_MASK_CURRENT_LIMIT (BIT(7))
#define IP802AR_MASK_POWER_LIMIT (BIT(6))
#define IP802AR_MASK_VICTIM_STRATEGY (GENMASK(2, 0))

/* Available current */
#define IP802AR_PAGE_CURRENT (1)
#define IP802AR_REG_AVAILABLE_CURRENT (0x54)
#define IP802AR_MASK_AVAILABLE_CURRENT_MSB (GENMASK(6, 0))
#define IP802AR_MASK_AVAILABLE_CURRENT_LSB (GENMASK(7, 0))
#define IP802AR_REG_CONSUMED_CURRENT (0x56)
#define IP802AR_PAGE_POWER (1)
#define IP802AR_REG_AVAILABLE_POWER (0x6c)
#define IP802AR_REG_CONSUMED_POWER (0x6a)
/* Datasheet has this in two places with two different registers */
#define _IP802AR_PAGE_AVAILABLE_POWER (2)
#define _IP802AR_REG_AVAILABLE_POWER (0x2e)

/* PSE Allocated Power */
#define IP802AR_PAGE_PSE_ALLOCATED_POWER (1)
#define IP802AR_REG_PSE_ALLOCATED_POWER (0x6e)

struct ip802ar_priv {
	int i2c_fd;

	int i2c_addr;
};

enum operation_mode {
	AUTO_MODE = 0x00,
	MANUAL_MODE = BIT(6),
	DIAGNOSTIC_MODE = BIT(7),
	SCAN_MODE = GENMASK(7, 6),
};

int ip802ar_init(struct poemgr_pse_chip *pse_chip, int i2c_bus, int i2c_addr,
		 uint32_t port_mask);
int ip802ar_device_online(struct poemgr_pse_chip *pse_chip);
int ip802ar_device_enable_set(struct poemgr_pse_chip *pse_chip,
			      int enable_disable);
int ip802ar_port_enable_set(struct poemgr_pse_chip *pse_chip, int port,
			    int enable_disable);
int ip802ar_port_enable_get(struct poemgr_pse_chip *pse_chip, int port);
int ip802ar_port_power_consumption_get(struct poemgr_pse_chip *pse_chip,
				       int port);
int ip802ar_port_afat_mode_set(struct poemgr_pse_chip *pse_chip, int port,
			       int mode);
int ip802ar_port_detection_classification_set(struct poemgr_pse_chip *pse_chip,
					      int port, int enable);
int ip802ar_port_poe_class_get(struct poemgr_pse_chip *pse_chip, int port);
int ip802ar_port_good_get(struct poemgr_pse_chip *pse_chip, int port);
int ip802ar_port_power_limit_get(struct poemgr_pse_chip *pse_chip, int port);
int ip802ar_system_power_budget_set(struct poemgr_pse_chip *pse_chip, int bank,
				    int val);
int ip802ar_system_current_budget_set(struct poemgr_pse_chip *pse_chip,
				      int milliamps);
int ip802ar_port_faults_get(struct poemgr_pse_chip *pse_chip, int port);
int ip802ar_clear_faults(struct poemgr_pse_chip *pse_chip, int num_ports);
int ip802ar_export_port_metric(struct poemgr_pse_chip *pse_chip, int port,
			       const struct poemgr_port_status *p_status,
			       struct poemgr_metric *output, int metric);
int ip802ar_export_metric(struct poemgr_pse_chip *pse_chip,
			  struct poemgr_metric *output, int metric);
