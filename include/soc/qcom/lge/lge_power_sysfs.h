/*
 * LGE charging scenario Header file.
 *
 * Copyright (C) 2013 LG Electronics
 * sangwoo <sangwoo2.park@lge.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LGE_POWER_SYSFS_H_
#define __LGE_POWER_SYSFS_H_

#define PWR_SYSFS_GROUPS_NUM 9

#if IS_ENABLED(CONFIG_LGE_DISPLAY_DUAL_BACKLIGHT)
#ifdef	PWR_SYSFS_GROUPS_NUM
#undef	PWR_SYSFS_GROUPS_NUM
#endif
#define PWR_SYSFS_GROUPS_NUM 10
#endif

#define PWR_SYSFS_MANDATORY_MAX_NUM 5

struct power_sysfs_array {
	const char *group;
	const char *user_node;
	const char *kernel_node;
};

const char *mandatory_paths[PWR_SYSFS_MANDATORY_MAX_NUM * 2] = {
	"adc", "xo_therm",
	"adc", "batt_therm",
	"battery", "status",
	"battery", "health",
	"charger", "online"
};

const char *group_names[PWR_SYSFS_GROUPS_NUM] = {
	"adc",
	"battery",
	"charger",
	"lcd",
#if IS_ENABLED(CONFIG_LGE_DISPLAY_DUAL_BACKLIGHT)
	"lcd_ex",
#endif
	"key_led",
	"cpu",
	"gpu",
	"platform",
	"testmode"
};

/* Set sysfs node for non-using DT */
#define PWR_SYSFS_PATH_NUM 51

#if IS_ENABLED(CONFIG_LGE_DISPLAY_DUAL_BACKLIGHT)
#ifdef	PWR_SYSFS_PATH_NUM
#undef	PWR_SYSFS_PATH_NUM
#endif
#define PWR_SYSFS_PATH_NUM 53
#endif

const char *default_pwr_sysfs_path[PWR_SYSFS_PATH_NUM][3] = {
	/* ADC/MPP */
	{"adc", "thermal", "/sys/class/thermal/"},
	{"adc", "xo_therm", "/sys/class/hwmon/hwmon2/device/xo_therm"},
	{"adc", "batt_therm", "/sys/class/power_supply/battery/temp"},
	{"adc", "batt_id", "/sys/class/power_supply/battery/valid_batt_id"},
	{"adc", "pa_therm0", "/sys/class/hwmon/hwmon2/device/pa_therm0"},
	{"adc", "pa_therm1", "NULL"},
	{"adc", "usb_in", "/sys/class/hwmon/hwmon3/device/usbin"},
	{"adc", "vcoin", "/sys/class/hwmon/hwmon2/device/vcoin"},
	{"adc", "vph_pwr", "/sys/class/hwmon/hwmon2/device/vph_pwr"},
	{"adc", "usb_id", "/sys/class/hwmon/hwmon2/device/usb_id_lv"},
	/* Battery */
	{"battery", "capacity", "/sys/class/power_supply/battery/capacity"},
	{"battery", "health", "/sys/class/power_supply/battery/health"},
	{"battery", "present", "/sys/class/power_supply/battery/present"},
	{"battery", "pseudo_batt", "/sys/class/power_supply/battery/pseudo_batt"},
	{"battery", "status", "/sys/class/power_supply/battery/status"},
	{"battery", "temp", "/sys/class/power_supply/battery/temp"},
	{"battery", "valid_batt_id", "/sys/class/power_supply/battery/valid_batt_id"},
	{"battery", "voltage_now", "/sys/class/power_supply/battery/voltage_now"},
	/* Charger */
	{"charger", "ac_online", "NULL"},
	{"charger", "usb_online", "/sys/class/power_supply/usb/online"},
	{"charger", "present", "/sys/class/power_supply/usb/present"},
	{"charger", "wlc_online", "NULL"},
	{"charger", "type", "/sys/class/power_supply/usb/type"},
	{"charger", "time_out", "/sys/class/power_supply/battery/safety_timer_enabled"},
	{"charger", "charging_enabled", "/sys/class/power_supply/battery/charging_enabled"},
	{"charger", "ibat_current", "/sys/class/power_supply/battery/current_now"},
	{"charger", "ichg_current", "/sys/class/power_supply/usb/current_max"},
	{"charger", "iusb_control", "NULL"},
	{"charger", "thermal_mitigation", "/sys/class/power_supply/battery/system_temp_level"},
	{"charger", "wlc_thermal_mitigation", "NULL"},
	{"charger", "usb_parallel_chg_status", "/sys/class/power_supply/usb-parallel/status"},
	{"charger", "usb_parallel_charging_enabled", "/sys/class/power_supply/usb-parallel/charging_enabled"},
	/* LCD Backlight */
	{"lcd", "brightness", "/sys/class/leds/lcd-backlight/brightness"},
	{"lcd", "max_brightness", "/sys/class/leds/lcd-backlight/max_brightness"},
	/* KEY LED */
	{"key_led", "red_brightness", "/sys/class/leds/red/brightness"},
	{"key_led", "green_brightness", "/sys/class/leds/green/brightness"},
	{"key_led", "blue_brightness", "/sys/class/leds/blue/brightness"},
	/* CPU */
	{"cpu", "cpu_idle_modes", "/sys/module/lpm_levels/system/"},
	/* GPU */
	{"gpu", "busy", "/sys/class/kgsl/kgsl-3d0/gpubusy"},
	/* PLATFORM */
	{"platform", "speed_bin", "NULL"},
	{"platform", "pvs_bin", "NULL"},
	{"platform", "power_state", "/sys/power/autosleep"},
	{"platform", "poweron_alarm", "/sys/module/qpnp_rtc/parameters/poweron_alarm"},
	{"platform", "pcb_rev", "/sys/class/hwmon/hwmon2/device/pcb_rev"},
	/* testmode */
	{"testmode", "charge", "/sys/class/power_supply/battery/charging_enabled"},
	{"testmode", "chcomp", "/sys/class/power_supply/battery/device/at_chcomp"},
	{"testmode", "chgmodeoff", "/sys/class/power_supply/battery/charging_enabled"},
	{"testmode", "fuelrst", "/sys/bus/i2c/devices/6-0036/fuelrst"},
	{"testmode", "rtc_time", "/dev/rtc0"},
	{"testmode", "pmrst", "/sys/class/power_supply/battery/device/at_pmrst"},
	{"testmode", "battexit", "/sys/class/power_supply/battery/present"},
};
#endif
