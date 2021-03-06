/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include <stdint.h>

#include "poemgr.h"

struct pd69104_priv {
	int i2c_fd;

	int i2c_addr;
};

int pd69104_init(struct poemgr_pse_chip *pse_chip, int i2c_bus, int i2c_addr, uint32_t port_mask);

int pd69104_end(struct poemgr_pse_chip *pse_chip);

int pd69104_device_online(struct poemgr_pse_chip *pse_chip);

int pd69104_port_power_consumption_get(struct poemgr_pse_chip *pse_chip, int port);

int pd69104_pwrgd_pin_status_get(struct poemgr_pse_chip *pse_chip);

int pd69104_port_operation_mode_get(struct poemgr_pse_chip *pse_chip, int port);

int pd69104_port_operation_mode_set(struct poemgr_pse_chip *pse_chip, int port, int opmode);

int pd69104_port_detection_classification_set(struct poemgr_pse_chip *pse_chip, int port, int enable);

int pd69104_port_poe_class_get(struct poemgr_pse_chip *pse_chip, int port);

int pd69104_port_power_enabled_get(struct poemgr_pse_chip *pse_chip, int port);

int pd69104_port_power_good_get(struct poemgr_pse_chip *pse_chip, int port);

int pd69104_port_power_limit_get(struct poemgr_pse_chip *pse_chip, int port);

int pd69104_port_power_limit_set(struct poemgr_pse_chip *pse_chip, int port, int val);

int pd69104_system_power_budget_get(struct poemgr_pse_chip *pse_chip, int bank);

int pd69104_system_power_budget_set(struct poemgr_pse_chip *pse_chip, int bank, int val);

int pd69104_port_faults_get(struct poemgr_pse_chip *pse_chip, int port);

int pd69104_export_metric(struct poemgr_pse_chip *pse_chip, struct poemgr_metric *output, int metric);
