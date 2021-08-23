#pragma once

/**
 * See Microsemi_PoE_PD69104B1_Generic_UG_Reg_Map.pdf
 *
 * http://ww1.microchip.com/downloads/en/DeviceDoc/Microsemi_PoE_PD69104B1_Generic_UG_Reg_Map.pdf
 */

#define GENMASK(h, l) \
	(((~0UL) << (l)) & (~0UL >> (32 - 1 - (h))))


#define PD69104_REG_FIRMWARE			0x41

#define PD69104_REG_DEVID				0x43

/* Extended Auto Mode only */
#define PD69104_REG_VTEMP				0x70

#define PD69104_REG_PORT_SR_BASE		0x75
#define PD69104_REG_PORT_SR(x)			(PD69104_REG_PORT_SR_BASE + (x < 3 ? 0 : 1))

#define PD69104_REG_PRIO_CR				0x80

#define PD69104_REG_PWR_CR_BASE			0x81
#define PD69104_REG_PWR_CR(x)			(PD69104_REG_PWR_CR_BASE + x)
#define PD69104_REG_PWR_CR_PAL_MASK		GENMASK(5, 0)

#define PD69104_REG_PWR_BNK_BASE		0x89
#define PD69104_REG_PWR_BNK(x)			(PD69104_REG_PWR_BNK_BASE + x)

#define PD69104_REG_PWRGD						0x91
#define PD69104_REG_PWRGD_PIN_STATUS_MASK		GENMASK(6, 3)
#define PD69104_REG_PWRGD_PIN_STATUS_SHIFT		3

#define PD69104_REG_PORT_CONS_BASE		0x92
#define PD69104_REG_PORT_CONS(x)			(PD69104_REG_PORT_CONS_BASE + x)
