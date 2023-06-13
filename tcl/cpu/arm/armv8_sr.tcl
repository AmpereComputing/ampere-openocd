# SPDX-License-Identifier: GPL-2.0-or-later
#
# ARMv8 Architected System Register support
#
# Copyright (c) 2020-2023, Ampere Computing LLC
#

# Command Usage
#
# Execute TCL script to load the read_reg and write_reg routines
#
# read_reg target_id sys_reg_name ['sec' | 'nsec' | 'asis']
#    target_id    : OpenOCD ARMv8 core target
#    sys_reg_name : One of the reg names in the get_sr_encoding routine
#    'sec'  : perform system reg read at highest exception level and ns=0
#    'nsec' : perform system reg read at highest exception level and ns=1
#    'asis' : perform sys reg read at current exception level and ns setting
#
# write_reg target_id sys_reg_name write_value ['sec' | 'nsec' | 'asis']
#    target_id    : OpenOCD ARMv8 core target
#    sys_reg_name : One of the reg names in the get_sr_encoding routine
#    write_value  : The numeric value in hexadecimal or decimal
#    'sec'  : perform system reg write at highest exception level and ns=0
#    'nsec' : perform system reg write at highest exception level and ns=1
#    'asis' : perform sys reg write at current exception level and ns setting

proc get_sr_encoding {sys_reg_name} {
	set sr_encoding {}

	set sys_reg_name [string toupper $sys_reg_name]

	switch $sys_reg_name {
		BRBCR_EL1 {set sr_encoding {2 1 9 0 0}}
		BRBCR_EL2 {set sr_encoding {2 4 9 0 0}}
		BRBFCR_EL1 {set sr_encoding {2 1 9 0 1}}
		BRBIDR0_EL1 {set sr_encoding {2 1 9 2 0}}
		BRBINFn_EL1 {set sr_encoding {8 1 8 0 0}}
		BRBSRCn_EL1 {set sr_encoding {2 1 8 0 1}}
		BRBTGTn_EL1 {set sr_encoding {2 1 8 0 2}}
		BRBTS_EL1 {set sr_encoding {2 1 9 0 2}}
		BRB_IALL {set sr_encoding {1 1 7 2 4}}
		CNTPCT_EL0 {set sr_encoding {3 3 14 0 1}}
		CNTPS_CTL_EL1 {set sr_encoding {3 7 14 2 1}}
		CNTPS_CVAL_EL1 {set sr_encoding {3 7 14 2 2}}
		CNTPS_TVAL_EL1 {set sr_encoding {3 7 14 2 0}}
		CNTP_CVAL_EL0 {set sr_encoding {3 3 14 2 2}}
		CNTP_TVAL_EL0 {set sr_encoding {3 3 14 2 0}}
		CPACR_EL1 {set sr_encoding {3 0 1 0 2}}
		CPTR_EL2 {set sr_encoding {3 4 1 1 2}}
		CPTR_EL3 {set sr_encoding {3 6 1 1 2}}
		CURRENTEL {set sr_encoding {3 0 4 2 2}}
		ELR_EL1 {set sr_encoding {3 0 4 0 1}}
		ERRIDR_EL1 {set sr_encoding {3 0 5 3 0}}
		ERRSELR_EL1 {set sr_encoding {3 0 5 3 1}}
		ERXADDR_EL1 {set sr_encoding {3 0 5 4 3}}
		ERXCTLR_EL1 {set sr_encoding {3 0 5 4 1}}
		ERXFR_EL1 {set sr_encoding {3 0 5 4 0}}
		ERXMISC0_EL1 {set sr_encoding {3 0 5 5 0}}
		ERXMISC1_EL1 {set sr_encoding {3 0 5 5 1}}
		ERXPFGCDN_EL1 {set sr_encoding {3 0 5 4 6}}
		ERXPFGCTL_EL1 {set sr_encoding {3 0 5 4 5}}
		ERXPFGF_EL1 {set sr_encoding {3 0 5 4 4}}
		ERXSTATUS_EL1 {set sr_encoding {3 0 5 4 2}}
		ESR_EL1 {set sr_encoding {3 0 5 2 0}}
		FAR_EL1 {set sr_encoding {3 0 6 0 0}}
		HCR_EL2 {set sr_encoding {3 4 1 1 0}}
		ICC_CTLR_EL1 {set sr_encoding {3 0 12 12 4}}
		ICC_CTLR_EL3 {set sr_encoding {3 6 12 12 4}}
		ICC_DIR_EL1 {set sr_encoding {3 0 12 11 1}}
		ICC_EOIR0_EL1 {set sr_encoding {3 0 12 8 1}}
		ICC_HPPIR0_EL1 {set sr_encoding {3 0 12 8 2}}
		ICC_HPPIR1_EL1 {set sr_encoding {3 0 12 12 2}}
		ICC_IAR0_EL1 {set sr_encoding {3 0 12 8 0}}
		ICC_IAR1_EL1 {set sr_encoding {3 0 12 12 0}}
		ICC_IGRPEN0_EL1 {set sr_encoding {3 0 12 12 6}}
		ICC_IGRPEN1_EL1 {set sr_encoding {3 0 12 12 7}}
		ICC_IGRPEN1_EL3 {set sr_encoding {3 6 12 12 7}}
		ICC_RPR_EL1 {set sr_encoding {3 0 12 11 3}}
		ICC_SRE_EL1 {set sr_encoding {3 0 12 12 5}}
		ICC_SRE_EL3 {set sr_encoding {3 6 12 12 5}}
		ID_AA64DFR0_EL1 {set sr_encoding {3 0 0 5 0}}
		MDCR_EL3 {set sr_encoding {3 6 1 3 1}}
		MPIDR_EL1 {set sr_encoding {3 0 0 0 5}}
		PAR_EL1 {set sr_encoding {3 0 7 4 0}}
		SCR_EL3 {set sr_encoding {3 6 1 1 0}}
		SCTLR_EL1 {set sr_encoding {3 0 1 0 0}}
		SCTLR_EL2 {set sr_encoding {3 4 1 0 0}}
		SCTLR_EL3 {set sr_encoding {3 6 1 0 0}}
		SPSEL {set sr_encoding {3 0 4 2 0}}
		SP_EL0 {set sr_encoding {3 0 4 1 0}}
		SP_EL1 {set sr_encoding {3 4 4 1 0}}
		SP_EL2 {set sr_encoding {3 6 4 1 0}}
		VBAR_EL1 {set sr_encoding {3 0 12 0 0}}
		VBAR_EL3 {set sr_encoding {3 6 12 0 0}}
		default {set sr_encoding {"Unsupported system register name"}}
	}

	return $sr_encoding
}

proc read_reg_usage {} {
	echo {read_reg target_id sys_reg_name ['sec' | 'nsec' | 'asis']}
	echo {   target_id    : OpenOCD ARMv8 core target identifier}
	echo {   sys_reg_name : a supported system register}
	echo {   'sec'  : read at highest exception level and ns=0}
	echo {   'nsec' : read at highest exception level and ns=1}
	echo {   'asis' : read at current exception level and ns setting}
	echo {}
}

proc read_reg {args} {
	set num [llength $args]
	if { $num == 2 } {
		set target_id [lindex $args 0]
		set sys_reg_name [lindex $args 1]
	} elseif { $num == 3 } {
		set target_id [lindex $args 0]
		set sys_reg_name [lindex $args 1]
		set option [string tolower [lindex $args 2]]
		if { $option != {sec} && $option != {nsec} && $option != {asis} } {
			read_reg_usage
			return {Invalid read_reg parameters}
		}
	} else {
		read_reg_usage
		return {Invalid read_reg parameters}
	}

	set sr_enc [get_sr_encoding $sys_reg_name]

	if {[llength $sr_enc] == 5 } {
		if {$num == 2} {
			set result [$target_id aarch64 mrs [lindex $sr_enc 0] [lindex $sr_enc 1] [lindex $sr_enc 2] [lindex $sr_enc 3] [lindex $sr_enc 4]]
		} else {
			set result [$target_id aarch64 mrs $option [lindex $sr_enc 0] [lindex $sr_enc 1] [lindex $sr_enc 2] [lindex $sr_enc 3] [lindex $sr_enc 4]]
		}
	} else {
		set result "Unsupported system register name $sys_reg_name"
		read_reg_usage
	}

	return $result
}

proc write_reg_usage {} {
	echo {write_reg target_id sys_reg_name write_value ['sec' | 'nsec' | 'asis']}
	echo {   target_id    : OpenOCD ARMv8 core target identifier}
	echo {   sys_reg_name : a supported system register}
	echo {   write_value  : write value in hexadecimal or decimal}
	echo {   'sec'  : write at highest exception level and ns=0}
	echo {   'nsec' : write at highest exception level and ns=1}
	echo {   'asis' : write at current exception level and ns setting}
	echo {}
}

proc write_reg {args} {
	set num [llength $args]
	if { $num == 3 } {
		set target_id [lindex $args 0]
		set sys_reg_name [lindex $args 1]
		set write_value [lindex $args 2]
	} elseif { $num == 4 } {
		set target_id [lindex $args 0]
		set sys_reg_name [lindex $args 1]
		set write_value [lindex $args 2]
		set option [string tolower [lindex $args 3]]
		if { $option != {sec} && $option != {nsec} && $option != {asis} } {
			write_reg_usage
			return {Invalid write_reg parameters}
		}
	} else {
		write_reg_usage
		return {Invalid write_reg parameters}
	}

	set sr_enc [get_sr_encoding $sys_reg_name]

	if {[llength $sr_enc] == 5} {
		if {$num == 3} {
			set result [$target_id aarch64 msr [lindex $sr_enc 0] [lindex $sr_enc 1] [lindex $sr_enc 2] [lindex $sr_enc 3] [lindex $sr_enc 4] $write_value]
		} else {
			set result [$target_id aarch64 msr $option [lindex $sr_enc 0] [lindex $sr_enc 1] [lindex $sr_enc 2] [lindex $sr_enc 3] [lindex $sr_enc 4] $write_value]
		}
	} else {
		set result "Unsupported system register name $sys_reg_name"
		write_reg_usage
	}

	return $result
}

echo "Added Commands:"
read_reg_usage
write_reg_usage
