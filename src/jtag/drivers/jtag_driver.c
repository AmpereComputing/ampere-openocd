// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * JTAG Driver
 *
 * Copyright (C) 2020, 2025, Ampere Computing LLC
 *
 * Based on:
 * ftdi.c: (C) 2012 Andreas Fritiofson, <andreas.fritiofson@gmail.com>
 * jtag_dpi.c: (C) 2013 Franck Jullien, <elec4fun@gmail.com>
 *             (C) 2019-2020 Ampere Computing LLC
 */

#include <sys/ioctl.h>

#include <helper/uapi_linux_jtag.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <jtag/interface.h>

#include <fcntl.h>

#define JTAG_INSTANCE 0

/**
 * Note: When calculating the 8-bit aligned address below for the maximum
 * JTAG transfer data bit length, the JTAG_MAX_XFER_DATA_LEN define is
 * adjusted by one bit to account for a code bug in the open-source Linux
 * JTAG driver. Due to the driver bug, transfers of exact size
 * JTAG_MAX_XFER_DATA_LEN bits will fail.
 */
#define JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED (((JTAG_MAX_XFER_DATA_LEN) - 1) & (~0x7))
#define JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED_DIV8 ((JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED) >> 3)

int jtag_instance = JTAG_INSTANCE;
int jtag_hw_accel;
int jtag_fd;

#define MAX_BB_LEN 64
struct tck_bitbang bitbang[MAX_BB_LEN];

/* Local Function Prototypes */
static enum jtag_tapstate state_conversion(enum tap_state state);
static int move_to_state(enum tap_state end_state);
static int jtag_driver_get_speed(int *speed);
static int jtag_driver_set_speed(int speed);
static int jtag_driver_speed_div(int speed, int *khz);
static int jtag_driver_execute_scan(struct scan_command *scan);
static int jtag_driver_execute_runtest(int num_cycles, enum tap_state end_state);
static int jtag_driver_execute_stableclocks(struct stableclocks_command *stableclocks);
static int jtag_driver_reset(int trst, int srst);
static int jtag_driver_execute_sleep(struct sleep_command *sleep);
static int jtag_driver_execute_tms(struct tms_command *tms);
static int jtag_driver_execute_tlr_reset(enum tap_state end_state);
static int jtag_driver_execute_queue(struct jtag_command *cmd_queue);
static int jtag_driver_init(void);
static int jtag_driver_quit(void);

static enum jtag_tapstate state_conversion(enum tap_state state)
{
	enum jtag_tapstate endstate;

	switch (state) {
	case TAP_DREXIT2:
		endstate = JTAG_STATE_EXIT2DR;
		break;
	case TAP_DREXIT1:
		endstate = JTAG_STATE_EXIT1DR;
		break;
	case TAP_DRSHIFT:
		endstate = JTAG_STATE_SHIFTDR;
		break;
	case TAP_DRPAUSE:
		endstate = JTAG_STATE_PAUSEDR;
		break;
	case TAP_IRSELECT:
		endstate = JTAG_STATE_SELECTIR;
		break;
	case TAP_DRUPDATE:
		endstate = JTAG_STATE_UPDATEDR;
		break;
	case TAP_DRCAPTURE:
		endstate = JTAG_STATE_CAPTUREDR;
		break;
	case TAP_DRSELECT:
		endstate = JTAG_STATE_SELECTDR;
		break;
	case TAP_IREXIT2:
		endstate = JTAG_STATE_EXIT2IR;
		break;
	case TAP_IREXIT1:
		endstate = JTAG_STATE_EXIT1IR;
		break;
	case TAP_IRSHIFT:
		endstate = JTAG_STATE_SHIFTIR;
		break;
	case TAP_IRPAUSE:
		endstate = JTAG_STATE_PAUSEIR;
		break;
	case TAP_IDLE:
		endstate = JTAG_STATE_IDLE;
		break;
	case TAP_IRUPDATE:
		endstate = JTAG_STATE_UPDATEIR;
		break;
	case TAP_IRCAPTURE:
		endstate = JTAG_STATE_CAPTUREIR;
		break;
	case TAP_RESET:
		endstate = JTAG_STATE_TLRESET;
		break;
	default:
		LOG_ERROR("JTAG DRIVER ERROR: unknown JTAG state encountered %d", state);
		endstate = JTAG_STATE_IDLE;
	}

	return endstate;
}

/**
 * Function move_to_state moves the TAP controller from the current state to a
 * end_state through a path determined by the JTAG_SIOCSTATE IOCTL call.
 *
 * @param end_state is the destination state for the move.
 */
static int move_to_state(enum tap_state end_state)
{
	struct jtag_tap_state goal_state;
	struct jtag_mode jmode;
	int ret_errno;

	if (jtag_hw_accel) {
		jmode.feature = JTAG_XFER_MODE;
		jmode.mode = JTAG_XFER_SW_MODE;
		ret_errno = ioctl(jtag_fd, JTAG_SIOCMODE, &jmode);
		if (ret_errno < 0) {
			LOG_ERROR("JTAG DRIVER ERROR: IOCTL JTAG_XFER_MODE call fail for %s", __func__);
			LOG_ERROR("JTAG DRIVER ERROR: Unable to disable hardware mode");
			return ERROR_FAIL;
		}
	}

	goal_state.reset = JTAG_NO_RESET;
	goal_state.from = state_conversion(tap_get_state());
	goal_state.endstate = state_conversion(end_state);
	goal_state.tck = 0;

	ret_errno = ioctl(jtag_fd, JTAG_SIOCSTATE, &goal_state);
	if (ret_errno < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: IOCTL JTAG_SIOCSTATE call fail for %s", __func__);
		LOG_ERROR("JTAG DRIVER ERROR: state transition failed");
		return ERROR_FAIL;
	}

	tap_set_state(end_state);

	if (jtag_hw_accel) {
		jmode.feature = JTAG_XFER_MODE;
		jmode.mode = JTAG_XFER_HW_MODE;
		ret_errno = ioctl(jtag_fd, JTAG_SIOCMODE, &jmode);
		if (ret_errno < 0) {
			LOG_ERROR("JTAG DRIVER ERROR: IOCTL JTAG_XFER_MODE call fail for %s", __func__);
			LOG_ERROR("JTAG DRIVER ERROR: Unable to enable hardware mode");
			return ERROR_FAIL;
		}
	}

	return ERROR_OK;
}

static int jtag_driver_get_speed(int *speed)
{
	int local_speed;
	int ret_errno;

	ret_errno = ioctl(jtag_fd, JTAG_GIOCFREQ, &local_speed);
	if (ret_errno < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: IOCTL JTAG_XFER_MODE call fail for %s", __func__);
		LOG_ERROR("JTAG DRIVER ERROR: unable to retrieve JTAG TCK frequency");
		return ERROR_FAIL;
	}

	if (!speed)
		LOG_INFO("JTAG DRIVER INFO: Read JTAG TCK frequency of %u", local_speed);
	else
		*speed = local_speed;

	return ERROR_OK;
}

static int jtag_driver_set_speed(int expected_speed)
{
	int actual_speed;
	int ret_errno;
	int ret = ERROR_OK;

	ret_errno = ioctl(jtag_fd, JTAG_SIOCFREQ, &expected_speed);
	if (ret_errno < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: IOCTL JTAG_SIOCFREQ call fail for %s", __func__);
		LOG_ERROR("JTAG DRIVER ERROR: unable to program JTAG TCK frequency");
		return ERROR_FAIL;
	}

	ret = jtag_driver_get_speed(&actual_speed);
	if (ret != ERROR_OK)
		LOG_ERROR("JTAG DRIVER ERROR: Set requested JTAG TCK frequency "
			"to %u, unable to verify set frequency", expected_speed);
	else
		LOG_INFO("JTAG DRIVER INFO: Requested JTAG TCK frequency "
			"%u, actual frequency %u", expected_speed, actual_speed);

	return ret;
}

static int jtag_driver_speed_div(int speed, int *khz)
{
	*khz = speed / 1000;
	return ERROR_OK;
}

static int jtag_driver_khz(int khz, int *jtag_speed)
{
	*jtag_speed = khz * 1000;
	return ERROR_OK;
}

/**
 * jtag_driver_execute_scan - launches a IR-scan or DR-scan
 * @cmd: the command to launch
 *
 * Launch a JTAG IR-scan or DR-scan
 *
 * Returns ERROR_OK if OK, otherwise ERROR_XXX
 */
static int jtag_driver_execute_scan(struct scan_command *scan)
{
	enum scan_type type;
	enum tap_state end_state;
	struct jtag_xfer xfer;
	uint8_t *data_buf_start;
	uint8_t *data_buf_chunk;
	int num_bits;
	int ret_errno;
	int ret = ERROR_OK;

	type = jtag_scan_type(scan);

	LOG_DEBUG_IO("JTAG DRIVER DEBUG: scan %s, type:%d, end in %s",
		scan->ir_scan ? "IRSCAN" : "DRSCAN", type, tap_state_name(scan->end_state));

	num_bits = jtag_build_buffer(scan, &data_buf_start);
	if (!data_buf_start) {
		LOG_ERROR("jtag_build_buffer call failed, data_buf_start == NULL, "
			"file %s, line %d", __FILE__, __LINE__);
		return ERROR_FAIL;
	}
	data_buf_chunk = data_buf_start;

	while (num_bits > 0) {
		if (scan->ir_scan)
			xfer.type = JTAG_SIR_XFER;
		else
			xfer.type = JTAG_SDR_XFER;

		if (type == SCAN_IN) {
			/* From target to host */
			xfer.direction = JTAG_READ_XFER;
		} else if (type == SCAN_OUT) {
			/* From host to target */
			xfer.direction = JTAG_WRITE_XFER;
		} else {
			/* Full-duplex scan */
			xfer.direction = JTAG_READ_WRITE_XFER;
		}
		xfer.from = state_conversion(tap_get_state());
		xfer.padding = 0;
		if (num_bits > JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED) {
			xfer.length = (__u32)JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED;
			if (scan->ir_scan)
				xfer.endstate = state_conversion(TAP_IRPAUSE);
			else
				xfer.endstate = state_conversion(TAP_DRPAUSE);
		} else {
			xfer.length = (__u32)num_bits;
			xfer.endstate = state_conversion(scan->end_state);
		}
		xfer.tdio = (__u64)(uintptr_t)data_buf_chunk;

		ret_errno = ioctl(jtag_fd, JTAG_IOCXFER, &xfer);
		if (ret_errno < 0) {
			LOG_ERROR("JTAG DRIVER ERROR: IOCTL JTAG_IOCXFER call fail for %s", __func__);
			LOG_ERROR("JTAG DRIVER ERROR: unable to scan");
			ret = ERROR_FAIL;
		} else {
			if (num_bits > JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED) {
				if (scan->ir_scan)
					end_state = TAP_IRPAUSE;
				else
					end_state = TAP_DRPAUSE;
			} else {
				end_state = scan->end_state;
			}
			tap_set_state(end_state);

			if (type != SCAN_OUT)
				ret = jtag_read_buffer(data_buf_chunk, scan);
		}

		if (ret != ERROR_OK)
			break;

		num_bits -= MIN(num_bits, JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED);
		data_buf_chunk += JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED_DIV8;
	}
	if (tap_get_state() != scan->end_state && move_to_state(scan->end_state) != ERROR_OK) {
		LOG_ERROR("JTAG DRIVER ERROR: Fail in %s", __func__);
		ret = ERROR_FAIL;
	}

	free(data_buf_start);

	return ret;
}

static int jtag_driver_execute_runtest(int num_cycles, enum tap_state end_state)
{
	struct bitbang_packet bitbang_packet;
	int bitbang_length;
	int i, j;
	int ret_errno;

	LOG_DEBUG_IO("JTAG DRIVER DEBUG: runtest %i cycles, end in %s", num_cycles,
		tap_state_name(end_state));

	/* only do a move_to_state operation when we're not already in IDLE */
	if (tap_get_state() != TAP_IDLE && move_to_state(TAP_IDLE) != ERROR_OK) {
		LOG_ERROR("JTAG DRIVER ERROR: Fail in %s", __func__);
		return ERROR_FAIL;
	}

	bitbang_packet.data = bitbang;

	i = 0;
	while (i < num_cycles) {
		bitbang_length = num_cycles > MAX_BB_LEN ? MAX_BB_LEN : num_cycles;
		bitbang_packet.length = bitbang_length;
		for (j = 0; j < bitbang_length; j++) {
			bitbang[j].tms = (__u8)0;
			bitbang[j].tdi = (__u8)0; /* write: host to device */
			bitbang[j].tdo = (__u8)0; /* read: device to host */
		}

		ret_errno = ioctl(jtag_fd, JTAG_IOCBITBANG, &bitbang_packet);
		if (ret_errno < 0) {
			LOG_ERROR("JTAG DRIVER ERROR: IOCTL JTAG_IOCBITBANG call fail for %s", __func__);
			LOG_ERROR("JTAG DRIVER ERROR: couldn't execute runtest");
			return ERROR_FAIL;
		}

		i += bitbang_length;
	}

	if (tap_get_state() != end_state && move_to_state(end_state) != ERROR_OK) {
		LOG_ERROR("JTAG DRIVER ERROR: Fail in %s", __func__);
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

static int jtag_driver_execute_stableclocks(struct stableclocks_command *stableclocks)
{
	struct bitbang_packet bitbang_packet;
	int bitbang_length;
	int num_cycles = stableclocks->num_cycles;
	int i, j;
	int ret_errno;
	uint8_t tms;

	LOG_DEBUG_IO("JTAG DRIVER DEBUG: clocks %i while in %s", num_cycles,
		tap_state_name(tap_get_state()));

	tms = (tap_get_state() == TAP_RESET ? 1 : 0);

	bitbang_packet.data = bitbang;

	i = 0;
	while (i < num_cycles) {
		bitbang_length = num_cycles > MAX_BB_LEN ? MAX_BB_LEN : num_cycles;
		bitbang_packet.length = bitbang_length;
		for (j = 0; j < bitbang_length; j++) {
			bitbang[j].tms = (__u8)tms;
			bitbang[j].tdi = (__u8)0; /* write: host to device */
			bitbang[j].tdo = (__u8)0; /* read: device to host */
		}

		ret_errno = ioctl(jtag_fd, JTAG_IOCBITBANG, &bitbang_packet);
		if (ret_errno < 0) {
			LOG_ERROR("JTAG DRIVER ERROR: IOCTL JTAG_IOCBITBANG call fail for %s", __func__);
			LOG_ERROR("JTAG DRIVER ERROR: couldn't execute stableclocks");
			return ERROR_FAIL;
		}

		i += bitbang_length;
	}

	return ERROR_OK;
}

static int jtag_driver_execute_tlr_reset(enum tap_state end_state)
{
	struct tms_command tms;
	struct jtag_xfer xfer;
	uint32_t data_buf;
	int ret_errno;
	int ret;
	uint8_t bits;

	/* Use this TAP reset method for the ASPEED AST2500/2600
	 * JTAG Master Controller Software Mode and Hardware Mode #1.
	 */

	/* There are two issues involved with HW Mode #1 of this JTAG Master
	 * Controller.
	 */

	/* 1. Resetting the JTAG state machine to Test-Logic-Reset (TLR)
	 *    doesn't work with the ioctl() JTAG_SIOCSTATE call as it
	 *    does with Software Mode. The workaround is to force
	 *    a reset by holding TMS high and pulsing TCK five times.
	 */

	/* 2. After switching to HW Mode #1 and resetting the JTAG state
	 *    machine to TLR using the 5xTMS approach, the first SDR Scan read
	 *    returns incorrect data. The workaround is after switching to
	 *    HW Mode #1 and resetting to TLR state, perform a dummy DR read
	 *    (not write) and discard the result.
	 */

	bits = 0x1F;
	tms.num_bits = 5;
	tms.bits = &bits;
	ret = jtag_driver_execute_tms(&tms);
	if (ret != ERROR_OK) {
		LOG_ERROR("JTAG DRIVER ERROR: couldn't reset JTAG state machine");
		return ERROR_FAIL;
	}

	tap_set_state(TAP_RESET);

	/* Bug Workaround - perform the dummy DR read */
	xfer.type = JTAG_SDR_XFER;       /* Type is DR scan */
	xfer.direction = JTAG_READ_XFER; /* Only perform DR read, no write */
	xfer.from = state_conversion(tap_get_state());
	xfer.endstate = JTAG_STATE_TLRESET;
	xfer.padding = 0;
	xfer.length = (__u32)1;          /* Only a single bit is needed */
	xfer.tdio = (__u64)(uintptr_t)(&data_buf);  /* Location to store read result */

	ret_errno = ioctl(jtag_fd, JTAG_IOCXFER, &xfer);
	if (ret_errno < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: scan failed");
		return ERROR_FAIL;
	}

	if (tap_get_state() != end_state && move_to_state(end_state) != ERROR_OK) {
		LOG_ERROR("JTAG DRIVER ERROR: Fail in %s", __func__);
		return ERROR_FAIL;
	}

	LOG_INFO("JTAG DRIVER INFO: Successfully reset JTAG state machine");

	return ERROR_OK;
}

static int jtag_driver_reset(int trst, int srst)
{
	int ret_errno;
	int ret = ERROR_OK;

	LOG_DEBUG_IO("JTAG DRIVER DEBUG: reset trst: %i srst %i", trst, srst);

	ret_errno = ioctl(jtag_fd, JTAG_SIOCTRST, &trst);
	if (ret_errno < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: IOCTL SIOCTRST call fail for %s", __func__);
		if (trst == 0)
			LOG_ERROR("JTAG DRIVER ERROR: unable to drive TRST_N high (not active)");
		else
			LOG_ERROR("JTAG DRIVER ERROR: unable to drive TRST_N low (active)");
		ret = ERROR_FAIL;
	} else if (trst == 1) {
		LOG_INFO("JTAG DRIVER INFO: Successfully reset JTAG state machine");
		tap_set_state(TAP_RESET);
	}

	if (srst == 1) {
		LOG_ERROR("JTAG DRIVER ERROR: Can't assert SRST: nSRST signal is not defined");
		ret = ERROR_FAIL;
	}

	return ret;
}

static int jtag_driver_execute_sleep(struct sleep_command *sleep)
{
	LOG_DEBUG_IO("JTAG DRIVER DEBUG: sleep %" PRIi32 " usec while in %s",
		sleep->us,
		tap_state_name(tap_get_state()));

	jtag_sleep(sleep->us);

	return ERROR_OK;
}

static int jtag_driver_execute_tms(struct tms_command *tms)
{
	struct bitbang_packet bitbang_packet;
	int bitbang_length;
	int i, j, index;
	int ret_errno;
	int tms_num_bits = tms->num_bits;
	uint8_t tms_bits = tms->bits[0];

	LOG_DEBUG_IO("JTAG DRIVER DEBUG: TMS: %d bits", tms_num_bits);

	bitbang_packet.data = bitbang;

	index = 0;
	i = 0;
	while (i < tms_num_bits) {
		bitbang_length = tms_num_bits > MAX_BB_LEN ? MAX_BB_LEN : tms_num_bits;
		bitbang_packet.length = bitbang_length;
		for (j = 0; j < bitbang_length; j++) {
			bitbang[j].tms = (__u8)((tms_bits >> (i & 0x7)) & 0x1);
			bitbang[j].tdi = (__u8)0;
			bitbang[j].tdo = (__u8)0;

			i++;
			if ((i & 0x7) == 0) {
				index++;
				tms_bits = tms->bits[index];
			}
		}

		ret_errno = ioctl(jtag_fd, JTAG_IOCBITBANG, &bitbang_packet);
		if (ret_errno < 0) {
			LOG_ERROR("JTAG DRIVER ERROR: IOCTL JTAG_IOCBITBANG call fail for %s", __func__);
			LOG_ERROR("JTAG DRIVER ERROR: execute_tms failed");
			return ERROR_FAIL;
		}

		for (j = 0; j < bitbang_length; j++)
			tap_set_state(tap_state_transition(tap_get_state(), bitbang[j].tms));
	}

	return ERROR_OK;
}

static int jtag_driver_execute_queue(struct jtag_command *cmd_queue)
{
	struct jtag_command *cmd;
	int ret = ERROR_OK;

	for (cmd = cmd_queue; ret == ERROR_OK && cmd; cmd = cmd->next) {
		switch (cmd->type) {
		case JTAG_SCAN:
			ret = jtag_driver_execute_scan(cmd->cmd.scan);
			break;
		case JTAG_TLR_RESET:
			ret = jtag_driver_execute_tlr_reset(cmd->cmd.statemove->end_state);
			break;
		case JTAG_RUNTEST:
			ret = jtag_driver_execute_runtest(cmd->cmd.runtest->num_cycles,
							cmd->cmd.runtest->end_state);
			break;
		case JTAG_RESET:
			ret = jtag_driver_reset(cmd->cmd.reset->trst, cmd->cmd.reset->srst);
			break;
		case JTAG_PATHMOVE:
			break;
		case JTAG_SLEEP:
			ret = jtag_driver_execute_sleep(cmd->cmd.sleep);
			break;
		case JTAG_STABLECLOCKS:
			ret = jtag_driver_execute_stableclocks(cmd->cmd.stableclocks);
			break;
		case JTAG_TMS:
			ret = jtag_driver_execute_tms(cmd->cmd.tms);
			break;
		default:
			LOG_ERROR("JTAG DRIVER ERROR: unknown JTAG command type encountered 0x%X",
				  cmd->type);
			ret = ERROR_FAIL;
			break;
		}
	}

	return ret;
}

static int jtag_driver_init(void)
{
	struct jtag_mode jmode;
	int ret_errno;
	char buf[32];

	snprintf(buf, sizeof(buf), "/dev/jtag%u", jtag_instance);
	jtag_fd = open(buf, O_RDWR);
	if (jtag_fd < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: Could not open JTAG device");
		LOG_ERROR("JTAG DRIVER ERROR: Connection to /dev/jtag%u failed", jtag_instance);
		return ERROR_FAIL;
	}
	LOG_INFO("JTAG DRIVER INFO: Connection to /dev/jtag%u succeeded", jtag_instance);

	jmode.feature = JTAG_CONTROL_MODE;
	jmode.mode = JTAG_CONTROLLER_MODE;
	ret_errno = ioctl(jtag_fd, JTAG_SIOCMODE, &jmode);
	if (ret_errno < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: IOCTL JTAG_SIOCMODE call fail for %s", __func__);
		LOG_ERROR("JTAG DRIVER ERROR: unable to set JTAG_CONTROL_MODE");
		return ERROR_FAIL;
	}

	jmode.feature = JTAG_XFER_MODE;
	if (jtag_hw_accel == 0)
		jmode.mode = JTAG_XFER_SW_MODE;
	else
		jmode.mode = JTAG_XFER_HW_MODE;
	ret_errno = ioctl(jtag_fd, JTAG_SIOCMODE, &jmode);
	if (ret_errno < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: IOCTL JTAG_SIOCMODE call fail for %s", __func__);
		LOG_ERROR("JTAG DRIVER ERROR: unable to set JTAG_XFER_MODE");
		return ERROR_FAIL;
	} else if (jmode.mode == JTAG_XFER_HW_MODE) {
		LOG_INFO("JTAG DRIVER INFO: Hardware Acceleration mode enabled");
	} else {
		LOG_INFO("JTAG DRIVER INFO: Software mode enabled");
	}

	return ERROR_OK;
}

static int jtag_driver_quit(void)
{
	int ret = ERROR_OK;

	close(jtag_fd);

	return ret;
}

COMMAND_HANDLER(jtag_driver_set_instance)
{
	if (CMD_ARGC > 1)
		return ERROR_COMMAND_SYNTAX_ERROR;
	else if (CMD_ARGC == 1)
		COMMAND_PARSE_NUMBER(int, CMD_ARGV[0], jtag_instance);

	LOG_INFO("JTAG DRIVER INFO: Using /dev/jtag%u", jtag_instance);

	return ERROR_OK;
}

COMMAND_HANDLER(jtag_driver_hw_accel)
{
	if (CMD_ARGC > 1)
		return ERROR_COMMAND_SYNTAX_ERROR;
	else if (CMD_ARGC == 1)
		COMMAND_PARSE_NUMBER(int, CMD_ARGV[0], jtag_hw_accel);

	if (jtag_hw_accel == 0)
		LOG_INFO("JTAG DRIVER INFO: Using Software mode");
	else
		LOG_INFO("JTAG DRIVER INFO: Using Hardware Acceleration mode");

	return ERROR_OK;
}

static const struct command_registration jtag_driver_command_handlers[] = {
	{
		.name = "jtag_driver_set_instance",
		.handler = &jtag_driver_set_instance,
		.mode = COMMAND_CONFIG,
		.help = "set the instance of the JTAG device",
		.usage = "description_string",
	},
	{
		.name = "jtag_driver_hw_accel",
		.handler = &jtag_driver_hw_accel,
		.mode = COMMAND_CONFIG,
		.help = "enable or disable JTAG controller hardware acceleration",
		.usage = "description_string",
	},
	COMMAND_REGISTRATION_DONE
};

static struct jtag_interface jtag_driver_interface = {
	.supported = DEBUG_CAP_TMS_SEQ,
	.execute_queue = jtag_driver_execute_queue,
};

struct adapter_driver jtag_driver_adapter_driver = {
	.name = "jtag_driver",
	.transport_ids = TRANSPORT_JTAG,
	.transport_preferred_id = TRANSPORT_JTAG,
	.commands = jtag_driver_command_handlers,

	.init = jtag_driver_init,
	.quit = jtag_driver_quit,
	.reset = jtag_driver_reset,
	.speed = jtag_driver_set_speed,
	.khz = jtag_driver_khz,
	.speed_div = jtag_driver_speed_div,
	.jtag_ops = &jtag_driver_interface,
};
