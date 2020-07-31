/*
 * JTAG Driver
 *
 * Copyright (C) 2020, Ampere Computing LLC
 *
 * Based on:
 * ftdi.c: (C) 2012 Andreas Fritiofson, <andreas.fritiofson@gmail.com>
 * jtag_dpi.c: (C) 2013 Franck Jullien, <elec4fun@gmail.com>
 *             (C) 2019-2020 Ampere Computing LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/ioctl.h>

#include <helper/uapi_linux_jtag.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <jtag/interface.h>

#include <fcntl.h>

#define JTAG_INSTANCE 0

int jtag_instance = JTAG_INSTANCE;
int jtag_fd;

static enum jtag_endstate state_conversion(tap_state_t state)
{
	enum jtag_endstate endstate;

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
			LOG_ERROR("JTAG DRIVER ERROR: unknown JTAG state encountered 0x%d", state);
			endstate = JTAG_STATE_IDLE;
	}

	return endstate;
}

/**
 * Function move_to_state
 * moves the TAP controller from the current state to a
 * goal_state through a path given by tap_get_tms_path().
 * State transition logging is performed by delegation to clock_tms().
 *
 * @param goal_state is the destination state for the move.
 */
static int move_to_state(tap_state_t goal_state)
{
	struct jtag_end_tap_state end_state;
	int ret = ERROR_OK;
	int ret_errno;

	end_state.reset = JTAG_NO_RESET;
	end_state.endstate = state_conversion(goal_state);
	end_state.tck = 0;

	ret_errno = ioctl(jtag_fd, JTAG_SIOCSTATE, &end_state);
	if (ret_errno < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: state transition failed");
		ret = ERROR_FAIL;
	} else
		tap_set_state(goal_state);

	return ret;
}

static int jtag_driver_speed(int speed)
{
	int ret = ERROR_OK;
	int ret_errno;

	ret_errno = ioctl(jtag_fd, JTAG_SIOCFREQ, &speed);
	if (ret_errno < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: couldn't set JTAG TCK frequency");
		ret = ERROR_FAIL;
	} else
		LOG_INFO("JTAG DRIVER INFO: Setting JTAG TCK frequency to %u", speed);

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

static void jtag_driver_end_state(tap_state_t state)
{
	if (tap_is_state_stable(state))
		tap_set_end_state(state);
	else {
		LOG_ERROR("JTAG DRIVER ERROR: %s is not a stable end state", tap_state_name(state));
		exit(-1);
	}
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
	struct jtag_xfer xfer;
	enum scan_type type;
	uint8_t *data_buf;
	int num_bits;
	int ret = ERROR_OK;
	int ret_errno;

	type = jtag_scan_type(scan);

	LOG_DEBUG_IO("JTAG DRIVER DEBUG: %s type:%d", scan->ir_scan ? "IRSCAN" : "DRSCAN",
		type);

	num_bits = jtag_build_buffer(scan, &data_buf);
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

	xfer.length = (__u32)num_bits;
	xfer.tdio = (__u64)data_buf;
	xfer.endstate = state_conversion(scan->end_state);

	ret_errno = ioctl(jtag_fd, JTAG_IOCXFER, &xfer);
	if (ret_errno < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: unable to scan");
		ret = ERROR_FAIL;
	} else {
		tap_set_state(scan->end_state);

		if (type != SCAN_OUT)
			ret = jtag_read_buffer(data_buf, scan);

		LOG_DEBUG_IO("JTAG DRIVER DEBUG: %s scan, %i bits, end in %s",
			(scan->ir_scan) ? "IR" : "DR", num_bits,
			tap_state_name(scan->end_state));
	}

	free(data_buf);

	return ret;
}

static void jtag_driver_execute_statemove(struct statemove_command *statemove)
{
	LOG_DEBUG_IO("JTAG DRIVER DEBUG: statemove end in %s",
		tap_state_name(statemove->end_state));

	jtag_driver_end_state(statemove->end_state);

	/* shortest-path move to desired end state */
	if (tap_get_state() != tap_get_end_state() || tap_get_end_state() == TAP_RESET)
		move_to_state(tap_get_end_state());
}

static int jtag_driver_execute_runtest(int num_cycles, tap_state_t state)
{
	struct tck_bitbang bitbang;
	int i;
	int ret = ERROR_OK;
	int ret_errno;

	LOG_DEBUG_IO("JTAG DRIVER DEBUG: runtest %i cycles, end in %s", num_cycles,
		tap_state_name(state));

	if (tap_get_state() != TAP_IDLE)
		move_to_state(TAP_IDLE);

	bitbang.tms = (__u8)0;
	bitbang.tdi = (__u8)0; /* write: host to device */
	bitbang.tdo = (__u8)0; /* read: device to host */

	for (i = 0; i < num_cycles && ret == ERROR_OK; i++) {
		ret_errno = ioctl(jtag_fd, JTAG_IOCBITBANG, &bitbang);
		if (ret_errno < 0) {
			LOG_ERROR("JTAG DRIVER ERROR: couldn't execute runtest");
			ret = ERROR_FAIL;
		}
	}

	jtag_driver_end_state(state);

	if (tap_get_state() != tap_get_end_state())
		move_to_state(tap_get_end_state());

	LOG_DEBUG_IO("JTAG DRIVER DEBUG: runtest: %i, end in %s", num_cycles,
		tap_state_name(tap_get_end_state()));

	return ret;
}

static int jtag_driver_execute_stableclocks(struct stableclocks_command *stableclocks)
{
	int num_cycles = stableclocks->num_cycles;
	int ret = ERROR_OK;

	ret = jtag_driver_execute_runtest(num_cycles, TAP_IDLE);
	if (ret != ERROR_OK)
		LOG_ERROR("JTAG DRIVER ERROR: Fail in jtag_driver_execute_stableclocks()");
	else
		LOG_DEBUG_IO("JTAG DRIVER DEBUG: clocks %i while in %s", num_cycles,
			tap_state_name(tap_get_state()));

	return ret;
}

static int jtag_driver_reset(int trst, int srst)
{
	struct jtag_end_tap_state end_state;
	int ret = ERROR_OK;
	int ret_errno;

	LOG_DEBUG_IO("JTAG DRIVER DEBUG: reset trst: %i srst %i", trst, srst);

	if (trst == 1) {
		end_state.reset = JTAG_FORCE_RESET;
		end_state.endstate = JTAG_STATE_TLRESET;
		end_state.tck = 0;
		ret_errno = ioctl(jtag_fd, JTAG_SIOCSTATE, &end_state);
		if (ret_errno < 0) {
			LOG_ERROR("JTAG DRIVER ERROR: couldn't assert TRST");
			ret = ERROR_FAIL;
		} else
			tap_set_state(TAP_RESET);
	} else {
		end_state.reset = JTAG_FORCE_RESET;
		end_state.endstate = JTAG_STATE_IDLE;
		end_state.tck = 0;
		ret_errno = ioctl(jtag_fd, JTAG_SIOCSTATE, &end_state);
		if (ret_errno < 0) {
			LOG_ERROR("JTAG DRIVER ERROR: couldn't de-assert TRST");
			ret = ERROR_FAIL;
		} else
			tap_set_state(TAP_IDLE);
	}

	if (srst == 1) {
		LOG_ERROR("JTAG DRIVER ERROR: Can't assert SRST: nSRST signal is not defined");
		ret = ERROR_FAIL;
	}

	return ret;
}

static int jtag_driver_execute_sleep(struct sleep_command *sleep)
{
	LOG_DEBUG_IO("JTAG DRIVER DEBUG: sleep %" PRIi32, sleep->us);

	jtag_sleep(sleep->us);

	LOG_DEBUG_IO("JTAG DRIVER DEBUG: sleep %" PRIi32 " usec while in %s",
		sleep->us,
		tap_state_name(tap_get_state()));

	return ERROR_OK;
}

static int jtag_driver_execute_tms(struct tms_command *tms)
{
	struct tck_bitbang bitbang;
	int ret = ERROR_OK;
	int ret_errno;
	unsigned index, this_len, i, j;
	unsigned tms_num_bits = tms->num_bits;
	uint8_t tms_bits = tms->bits[0];

	LOG_DEBUG_IO("JTAG DRIVER DEBUG: TMS: %d bits", tms_num_bits);

	bitbang.tms = (__u8)0;
	bitbang.tdi = (__u8)0;
	bitbang.tdo = (__u8)0;

	index = 0;
	j = 0;

	while ((j < tms_num_bits) && (ret == ERROR_OK)) {
		this_len = tms_num_bits > 8 ? 8 : tms_num_bits;
		for (i = 0; i < this_len && ret == ERROR_OK; i++) {
			ret_errno = ioctl(jtag_fd, JTAG_IOCBITBANG, &bitbang);
			if (ret_errno < 0) {
				LOG_ERROR("JTAG DRIVER ERROR: execute_tms failed");
				ret = ERROR_FAIL;
			} else
				tap_set_state(tap_state_transition(tap_get_state(), (tms_bits >> i) & 1));
		}
		j += this_len;
		if (j < tms_num_bits) {
			index++;
			tms_bits = tms->bits[index];
		}
	}

	return ret;
}

static int jtag_driver_execute_queue(void)
{
	struct jtag_command *cmd;
	int ret = ERROR_OK;

	for (cmd = jtag_command_queue; ret == ERROR_OK && cmd != NULL;
	   cmd = cmd->next) {
		switch (cmd->type) {
		case JTAG_SCAN:
			ret = jtag_driver_execute_scan(cmd->cmd.scan);
			break;
		case JTAG_TLR_RESET:
			jtag_driver_execute_statemove(cmd->cmd.statemove);
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
	int ret = ERROR_OK;
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
	jmode.mode = JTAG_MASTER_MODE;   /* JTAG_MASTER_MODE or JTAG_SLAVE_MODE */
	ret_errno = ioctl(jtag_fd, JTAG_SIOCMODE, &jmode);
	if (ret_errno < 0) {
		LOG_ERROR("JTAG DRIVER ERROR: unable to set JTAG_CONTROL_MODE");
		ret = ERROR_FAIL;
	} else {
		jmode.feature = JTAG_XFER_MODE;
		jmode.mode = JTAG_XFER_SW_MODE;  /* JTAG_XFER_HW_MODE or JTAG_XFER_SW_MODE */
		ret_errno = ioctl(jtag_fd, JTAG_SIOCMODE, &jmode);
		if (ret_errno < 0) {
			LOG_ERROR("JTAG DRIVER ERROR: unable to set JTAG_XFER_MODE");
			ret = ERROR_FAIL;
		} else {
			ret = jtag_driver_speed(jtag_get_speed_khz() * 1000);
			if (ret != ERROR_OK)
				LOG_ERROR("JTAG DRIVER ERROR: Unable to set TCK frequency");
		}
	}

	return ret;
}

static int jtag_driver_quit(void)
{
	int ret = ERROR_OK;

	close(jtag_fd);

	return ret;
}

COMMAND_HANDLER(jtag_driver_set_instance)
{
	if (CMD_ARGC == 0)
		LOG_WARNING("JTAG DRIVER WARNING: Set the corresponding /dev/jtagX instance number");
	else
		COMMAND_PARSE_NUMBER(int, CMD_ARGV[0], jtag_instance);

	LOG_INFO("JTAG DRIVER INFO: Using /dev/jtag%u", jtag_instance);

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
	COMMAND_REGISTRATION_DONE
};

static struct jtag_interface jtag_driver_interface = {
	.supported = DEBUG_CAP_TMS_SEQ,
	.execute_queue = jtag_driver_execute_queue,
};

struct adapter_driver jtag_driver_adapter_driver = {
	.name = "jtag_driver",
	.transports = jtag_only,
	.commands = jtag_driver_command_handlers,

	.init = jtag_driver_init,
	.quit = jtag_driver_quit,
	.reset = jtag_driver_reset,
	.speed = jtag_driver_speed,
	.khz = jtag_driver_khz,
	.speed_div = jtag_driver_speed_div,
	.jtag_ops = &jtag_driver_interface,
};
