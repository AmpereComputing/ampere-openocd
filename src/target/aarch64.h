/* SPDX-License-Identifier: GPL-2.0-or-later */

/***************************************************************************
 *   Copyright (C) 2015 by David Ung                                       *
 *   Copyright (C) 2019, Ampere Computing LLC                              *
 ***************************************************************************/

#ifndef OPENOCD_TARGET_AARCH64_H
#define OPENOCD_TARGET_AARCH64_H

#include "armv8.h"

#define AARCH64_COMMON_MAGIC 0x41413634U

#define CPUDBG_CPUID	0xD00
#define CPUDBG_CTYPR	0xD04
#define CPUDBG_TTYPR	0xD0C
#define ID_AA64PFR0_EL1	0xD20
#define ID_AA64DFR0_EL1	0xD28
#define CPUDBG_LOCKACCESS 0xFB0
#define CPUDBG_LOCKSTATUS 0xFB4

#define BRP_NORMAL 0
#define BRP_CONTEXT 1

#define AARCH64_PADDRDBG_CPU_SHIFT 13

enum aarch64_isrmasking_mode {
	AARCH64_ISRMASK_OFF,
	AARCH64_ISRMASK_ON,
};

enum aarch64_steponly_mode {
	AARCH64_STEPONLY_OFF,
	AARCH64_STEPONLY_ON,
};

enum aarch64_cti_mode {
	AARCH64_CTIMODE_LEGACY,
	AARCH64_CTIMODE_EXTEND
};

enum aarch64_bpcnt_mode {
	AARCH64_BPCNT_OFF,
	AARCH64_BPCNT_ON,
};

enum aarch64_wpcnt_mode {
	AARCH64_WPCNT_OFF,
	AARCH64_WPCNT_ON,
};

struct aarch64_brp {
	int used;
	int type;
	target_addr_t value;
	uint32_t control;
	uint8_t brpn;
};

struct aarch64_common {
	unsigned int common_magic;

	struct armv8_common armv8_common;

	/* Context information */
	uint32_t system_control_reg;
	uint32_t system_control_reg_curr;

	/* Breakpoint register pairs */
	int brp_num_context;
	int brp_num;
	int brp_num_available;
	struct aarch64_brp *brp_list;

	/* Watchpoint register pairs */
	int wp_num;
	int wp_num_available;
	struct aarch64_brp *wp_list;

	enum aarch64_isrmasking_mode isrmasking_mode;

	enum aarch64_steponly_mode step_only_mode;

	enum aarch64_cti_mode cti_mode;

	enum aarch64_bpcnt_mode bpcnt_mode;

	enum aarch64_wpcnt_mode wpcnt_mode;
};

static inline struct aarch64_common *
target_to_aarch64(struct target *target)
{
	return container_of(target->arch_info, struct aarch64_common, armv8_common.arm);
}

int impdef_register_commands(struct command_context *cmd_ctx);

#endif /* OPENOCD_TARGET_AARCH64_H */
