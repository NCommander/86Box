/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Genesys Logic GL518SM hardware monitoring chip.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/smbus.h>
#include <86box/hwm.h>

 
#define CLAMP(a, min, max)		(((a)< (min)) ? (min) : (((a) > (max)) ? (max) : (a)))
#define GL518SM_RPM_TO_REG(r, d)	((r) ? CLAMP(480000 / (r * d), 1, 255) : 0)
#define GL518SM_VOLTAGE_TO_REG(v)	(((v) / 19) & 0xff)
#define GL518SM_VDD_TO_REG(v)		((((v) * 4) / 95) & 0xff)


typedef struct {
    uint32_t	  local;
    hwm_values_t  *values;

    uint16_t regs[32];
    uint8_t addr_register;

    uint8_t smbus_addr;
} gl518sm_t;


static uint8_t	gl518sm_smbus_read_byte(uint8_t addr, void *priv);
static uint8_t	gl518sm_smbus_read_byte_cmd(uint8_t addr, uint8_t cmd, void *priv);
static uint16_t	gl518sm_smbus_read_word_cmd(uint8_t addr, uint8_t cmd, void *priv);
static uint16_t	gl518sm_read(gl518sm_t *dev, uint8_t reg);
static void	gl518sm_smbus_write_byte(uint8_t addr, uint8_t val, void *priv);
static void	gl518sm_smbus_write_byte_cmd(uint8_t addr, uint8_t cmd, uint8_t val, void *priv);
static void	gl518sm_smbus_write_word_cmd(uint8_t addr, uint8_t cmd, uint16_t val, void *priv);
static uint8_t	gl518sm_write(gl518sm_t *dev, uint8_t reg, uint16_t val);
static void	gl518sm_reset(gl518sm_t *dev);


#ifdef ENABLE_GL518SM_LOG
int gl518sm_do_log = ENABLE_GL518SM_LOG;


static void
gl518sm_log(const char *fmt, ...)
{
    va_list ap;

    if (gl518sm_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define gl518sm_log(fmt, ...)
#endif


static void
gl518sm_remap(gl518sm_t *dev, uint8_t addr)
{
    gl518sm_log("GL518SM: remapping to SMBus %02Xh\n", addr);

    smbus_removehandler(dev->smbus_addr, 1,
			gl518sm_smbus_read_byte, gl518sm_smbus_read_byte_cmd, gl518sm_smbus_read_word_cmd, NULL,
			gl518sm_smbus_write_byte, gl518sm_smbus_write_byte_cmd, gl518sm_smbus_write_word_cmd, NULL,
			dev);

    if (addr < 0x80) smbus_sethandler(addr, 1,
			gl518sm_smbus_read_byte, gl518sm_smbus_read_byte_cmd, gl518sm_smbus_read_word_cmd, NULL,
			gl518sm_smbus_write_byte, gl518sm_smbus_write_byte_cmd, gl518sm_smbus_write_word_cmd, NULL,
			dev);

    dev->smbus_addr = addr;
}


static uint8_t
gl518sm_smbus_read_byte(uint8_t addr, void *priv)
{
    gl518sm_t *dev = (gl518sm_t *) priv;
    return gl518sm_read(dev, dev->addr_register);
}


static uint8_t
gl518sm_smbus_read_byte_cmd(uint8_t addr, uint8_t cmd, void *priv)
{
    gl518sm_t *dev = (gl518sm_t *) priv;
    return gl518sm_read(dev, cmd);
}


static uint16_t
gl518sm_smbus_read_word_cmd(uint8_t addr, uint8_t cmd, void *priv)
{
    gl518sm_t *dev = (gl518sm_t *) priv;
    return gl518sm_read(dev, cmd);
}


static uint16_t
gl518sm_read(gl518sm_t *dev, uint8_t reg)
{
    uint16_t ret = dev->regs[reg & 0x1f];

    switch (reg) {
	case 0x07: case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c:
		/* two-byte registers: leave as-is */
		break;

	default:
		/* single-byte registers: duplicate low byte to high byte (real hardware behavior unknown) */
		ret |= (ret << 8);
		break;
    }

    gl518sm_log("GL518SM: read(%02X) = %04X\n", reg, ret);

    return ret;
}


static void
gl518sm_smbus_write_byte(uint8_t addr, uint8_t val, void *priv)
{
    gl518sm_t *dev = (gl518sm_t *) priv;
    dev->addr_register = val;
}


static void
gl518sm_smbus_write_byte_cmd(uint8_t addr, uint8_t cmd, uint8_t val, void *priv)
{
    gl518sm_t *dev = (gl518sm_t *) priv;
    gl518sm_write(dev, cmd, val);
}


static void
gl518sm_smbus_write_word_cmd(uint8_t addr, uint8_t cmd, uint16_t val, void *priv)
{
    gl518sm_t *dev = (gl518sm_t *) priv;
    gl518sm_write(dev, cmd, val);
}


static uint8_t
gl518sm_write(gl518sm_t *dev, uint8_t reg, uint16_t val)
{
    gl518sm_log("GL518SM: write(%02X, %04X)\n", reg, val);

    switch (reg) {
	case 0x00: case 0x01: case 0x04: case 0x07: case 0x0d: case 0x12: case 0x13: case 0x14: case 0x15:
		/* read-only registers */
		return 0;

	case 0x0a:
		dev->regs[0x13] = (val & 0xff);
		break;

	case 0x03:
		dev->regs[reg] = (val & 0xfc);

		if (val & 0x80) /* Init */
			gl518sm_reset(dev);
		break;

	case 0x0f:
		dev->regs[reg] = (val & 0xf8);

		/* update fan values to match the new divisor */
		dev->regs[0x07] = (GL518SM_RPM_TO_REG(dev->values->fans[0], 1 << ((dev->regs[0x0f] >> 6) & 0x3)) << 8);
		dev->regs[0x07] |= GL518SM_RPM_TO_REG(dev->values->fans[1], 1 << ((dev->regs[0x0f] >> 4) & 0x3));
		break;

	case 0x11:
		dev->regs[reg] = (val & 0x7f);
		break;

	default:
		dev->regs[reg] = val;
		break;
    }

    return 1;
}


static void
gl518sm_reset(gl518sm_t *dev)
{
    memset(dev->regs, 0, sizeof(dev->regs));

    dev->regs[0x00] = 0x80;
    dev->regs[0x01] = 0x80; /* revision 0x80 can read all voltages */
    dev->regs[0x04] = ((dev->values->temperatures[0] + 119) & 0xff);
    dev->regs[0x05] = 0xc7;
    dev->regs[0x06] = 0xc2;
    dev->regs[0x07] = ((GL518SM_RPM_TO_REG(dev->values->fans[0], 8) << 8) | GL518SM_RPM_TO_REG(dev->values->fans[1], 8));
    dev->regs[0x08] = 0x6464;
    dev->regs[0x09] = 0xdac5;
    dev->regs[0x0a] = 0xdac5;
    dev->regs[0x0b] = 0xdac5;
    dev->regs[0x0c] = 0xdac5;
    /* AOpen System Monitor requires an approximate voltage offset of 13 at least on 3.3V (voltages[2]) */
    dev->regs[0x0d] = 13 + GL518SM_VOLTAGE_TO_REG(dev->values->voltages[2]);
    dev->regs[0x0f] = 0xf8;
    dev->regs[0x13] = 13 + GL518SM_VOLTAGE_TO_REG(dev->values->voltages[1]);
    dev->regs[0x14] = 13 + GL518SM_VOLTAGE_TO_REG(dev->values->voltages[0]);
    dev->regs[0x15] = 13 + GL518SM_VDD_TO_REG(5000);
}


static void
gl518sm_close(void *priv)
{
    gl518sm_t *dev = (gl518sm_t *) priv;

    gl518sm_remap(dev, 0);

    free(dev);
}


static void *
gl518sm_init(const device_t *info)
{
    gl518sm_t *dev = (gl518sm_t *) malloc(sizeof(gl518sm_t));
    memset(dev, 0, sizeof(gl518sm_t));

    dev->local = info->local;
    
    /* Set default values. */
    hwm_values_t defaults = {
	{    /* fan speeds */
		3000,	/* System */
		3000	/* CPU */
	}, { /* temperatures */
		30	/* CPU */
	}, { /* voltages */
		hwm_get_vcore(),		  /* Vcore */
		RESISTOR_DIVIDER(12000, 150, 47), /* +12V (15K/4.7K divider suggested in the GL518SM datasheet) */
		3300				  /* +3.3V */
	}
    };
    hwm_values = defaults;
    dev->values = &hwm_values;

    gl518sm_reset(dev);
    gl518sm_remap(dev, dev->local & 0x7f);

    return dev;
}


/* GL518SM on SMBus address 2Ch */
const device_t gl518sm_2c_device = {
    "Genesys Logic GL518SM Hardware Monitor",
    DEVICE_ISA,
    0x2c,
    gl518sm_init, gl518sm_close, NULL,
    NULL, NULL, NULL,
    NULL
};

/* GL518SM on SMBus address 2Dh */
const device_t gl518sm_2d_device = {
    "Genesys Logic GL518SM Hardware Monitor",
    DEVICE_ISA,
    0x2d,
    gl518sm_init, gl518sm_close, NULL,
    NULL, NULL, NULL,
    NULL
};
