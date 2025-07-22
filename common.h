/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include <linux/i2c.h>
#include <stdint.h>
#include <stdio.h>

#ifndef BIT
#define BIT(x) (1UL<< (x))
#endif /* BIT */

#ifndef GENMASK
#define GENMASK(msb, lsb) \
	((BIT((msb) - (lsb) + 1) - 1) << lsb)
#endif /* GENMASK */

#define GENMASK_OL(offset, len) (GENMASK(offset+len, offset))

#define debug(fmt, ...) fprintf(stderr, "%s +%d [%s]: " fmt, __FILE__, __LINE__, __func__ __VA_OPT__(,) __VA_ARGS__);



/**
 *
 * Parse integral and fractional values consisting of n bits to
 * smaller unit integral
 * example supply voltage: fraction bits 4,
 * 0x03 0x56
 * integer = 0x35 = 53 volt = 53000 mvolt
 * fractional = 0x60 = 0.375 volt = 375 mvolt
 * smaller unit integer = 53375 mvolt
 *
 */
int32_t parse_nbit_value_munit(int val1, int val2, int fraction_bits);
int i2c_write(int i2c_fd, uint8_t addr, uint8_t reg, uint8_t data);
int i2c_read_nbytes(int i2c_fd, uint8_t i2c_addr, uint8_t reg, size_t n,
		    void *val);
int i2c_read(int i2c_fd, uint8_t i2c_addr, uint8_t reg, uint8_t *val);
