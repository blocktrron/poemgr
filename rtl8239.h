/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include <stdint.h>

#include "poemgr.h"

#define ENUM_CASE2STR(e) \
	case e:          \
		return #e

enum rtl8239_device_id {
	RTL8238B = 0x0138,
	RTL8238C = 0x0238,
	RTL8239 = 0x0039,
	RTL8239C = 0x0139,
	DEVICE_ID_UNKNOWN = 0
};

enum rtl8239_error_type {
	OVLO, // Over voltage lockout
	MPS_ABSENT,
	SHORT,
	OVERLOAD,
	POWER_DENIED,
	THERMAL_SHUTDOWN,
	INRUSH_FAIL,
	UVLO, // Under voltage lockout
	GOTP,

	NO_ERROR,
	ERROR_TYPE_ERROR
};

enum rtl8239_classification_result {
	PD0 = 0,
	PD1 = 1,
	PD2 = 2,
	PD3 = 3,
	PD4 = 4,
	PD5 = 5,
	PD6 = 6,
	PD7 = 7,
	PD8 = 8,
	PD_TREATED_AS_CLASS0,
	CLASS_MISMATCH,
	CLASS_OVERCURRENT,

	CLASSIFICATION_ERROR
};

enum rtl8239_detection_result {
	DETECTION_UNKNOWN,
	SHORT_CIRCUIT,
	HIGH_CAP,
	RLOW,
	VALID_PD,
	RHIGH,
	OPEN_CIRCUIT,
	FET_FAIL,

	DETECTION_ERROR
};

enum rtl8239_power_status {
	DISABLED,
	SEARCHING,
	DELIVERING_POWER,
	FAULT,
	REQUESTING_POWER,

	POWER_STATUS_ERROR
};

enum rtl8239_connection_check_result {
	TWO_PAIR,
	SINGLE_PD,
	DUAL_PD,
	CONNECTION_CHECK_UNKNOWN,

	CONNECTION_CHECK_ERROR
};

enum rtl8239_port_max_power_type {
	UNKNOWN = 0x0,
	CLASS_BASED = 0x01,
	USER_DEFINED = 0x02,
};

enum rtl8239_power_management_mode {
	NONE = 0x0,
	STATIC_WITH_PRIO,
	DYNAMIC_WITH_PRIO,
	STATIC_NO_PRIO,
	DYNAMIC_NO_PRIO,
};

struct rtl8239_priv {
	int i2c_fd;
	int i2c_addr;
};

struct rtl8239_system_status {
	uint8_t max_ports;
	uint8_t poe_enabled;
	// uint16_t _device_id;
	enum rtl8239_device_id device_id;
	uint8_t sw_version;
	char sw_version_str[16];
	uint8_t mcu_type;
	char mcu_type_str[32];
	uint8_t config_is_dirty;
	uint8_t system_reset_happened;
	uint8_t global_disable_low;
	uint8_t ext_ver;
	char ext_ver_str[16];
};

struct rtl8239_system_power_status {
	int bank_id;
	uint32_t allocated_power;
	uint32_t available_power;
	uint32_t current_power;
	enum rtl8239_power_management_mode bank_mgmt_mode;
	int bank_disable_prealloc;
	uint32_t bank_total_power;
	uint32_t bank_reserved_power;
};

struct rtl8239_port_status {
	int port;
	enum rtl8239_power_status power_status;
	enum rtl8239_error_type error_type;
	enum rtl8239_detection_result detection_result;
	enum rtl8239_classification_result classification_result,
		primary_channel_classification_result,
		secondary_channel_classification_result,
		valid_channel_classification_result;
	enum rtl8239_connection_check_result connection_check_result;

	// Port Mib Counters
	uint32_t mps_absent_counter;
	uint32_t overload_counter;
	uint32_t short_counter;
	uint32_t power_denied_counter;
	uint32_t invalid_signature_counter;

	// Port Basic Config 0x48
	int is_enabled;
	int function_mode;
	int det_type;
	int disconnect_type;
	int pair_type;
	int cable_type;

	// Port Extended Config 0x49
	int inrush_mode;
	int limit_type;
	uint32_t max_power;
	int priority;
	int chipaddr;
	int primary_channel;
	int secondary_channel;
};

struct rtl8239_port_measurement {
	int port;
	uint32_t voltage;
	uint32_t current;
	uint32_t temperature;
	uint32_t power;
};

int rtl8239_init(struct poemgr_pse_chip *pse_chip, int i2c_bus, int i2c_addr,
		 uint32_t port_mask);

int rtl8239_device_online(struct poemgr_pse_chip *pse_chip);
int rtl8239_device_reset(struct poemgr_pse_chip *pse_chip);
int rtl8239_global_enable(struct poemgr_pse_chip *pse_chip, int enable_disable);
int rtl8239_port_status_get(struct poemgr_pse_chip *pse_chip,
			    struct rtl8239_port_status *p_status);
int rtl8239_port_measurement_get(struct poemgr_pse_chip *pse_chip,
				 struct rtl8239_port_measurement *p_measurement);
int rtl8239_port_enable_set(struct poemgr_pse_chip *pse_chip, int port,
			    int enable_disable);
int rtl8239_global_power_management_mode_set(
	struct poemgr_pse_chip *pse_chip,
	struct rtl8239_system_power_status status);
int rtl8239_global_power_source_set(struct poemgr_pse_chip *pse_chip,
				    uint32_t power_budget_watts);
int rtl8239_port_max_power_type_set(struct poemgr_pse_chip *pse_chip, int port,
				    enum rtl8239_port_max_power_type power_type);
int rtl8239_export_port_metric(struct poemgr_pse_chip *pse_chip, int port,
			       struct poemgr_metric *output, int metric);
