/* Lge_touch_core.c
 *
 * Copyright (C) 2011 LGE.
 *
 * Author: yehan.ahn@lge.com, hyesung.shin@lge.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/list.h>
#include <linux/wakelock.h>
#include <linux/atomic.h>
//#include <mach/board.h>
#include <soc/qcom/lge/board_lge.h>
#include <linux/regulator/consumer.h>
#include <linux/input/lge_touch_core.h>
#include "./DS5/RefCode_F54.h"
#include <linux/moduleparam.h>

#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#elif defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

#include <linux/input/lge_touch_notify.h>

struct touch_device_driver      *touch_device_func;
struct workqueue_struct         *touch_wq;
int lockscreen_stat = 0;
int ime_stat = 0;
int quick_cover_status = 0;
int knock_mode = 0;
extern int boot_mode;
extern int mfts_enable;

struct timeval t_ex_debug[EX_PROFILE_MAX];
bool ghost_detection = 0;
bool long_press_check = 0;
unsigned int ghost_detection_count = 0;
unsigned int ta_debouncing_count = 0;
unsigned int finger_subtraction_count = 0;
unsigned int long_press_count = 0;
unsigned int button_press_count = 0;
unsigned int ts_rebase_count = 0;
unsigned int ta_rebase_retry_count = 0;
enum window_status window_crack_check = NO_CRACK;
int touch_thermal_status = 0;
int current_thermal_mode = 0;
extern struct pseudo_batt_info_type pseudo_batt_info;
int touch_ta_status = 0;
int touch_hdmi_status = 0;
u8  is_probe = 0;
extern int is_Sensing;
static struct lge_touch_data *ts_data = NULL;
int factory_boot = 0;

#define SENSING_TEST_PATH "/data/touch/sensing_test.txt"

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_I2C_RMI4
extern bool i2c_suspended;
#endif

extern void touch_enable_irq(unsigned int irq);
extern void touch_disable_irq(unsigned int irq);
/* Debug mask value
 * usage: echo [debug_mask] > /sys/module/lge_touch_core/parameters/debug_mask
 */
u32 touch_debug_mask = DEBUG_BASE_INFO | DEBUG_LPWG_COORDINATES;
module_param_named(debug_mask, touch_debug_mask, int, S_IRUGO|S_IWUSR|S_IWGRP);

#ifdef LGE_TOUCH_TIME_DEBUG
/* Debug mask value
 * usage: echo [debug_mask] >
 * /sys/module/lge_touch_core/parameters/time_debug_mask
 */
u32 touch_time_debug_mask = DEBUG_TIME_PROFILE_NONE;
module_param_named(time_debug_mask,
		touch_time_debug_mask, int, S_IRUGO|S_IWUSR|S_IWGRP);
#endif


/* set_touch_handle / get_touch_handle
 *
 * Developer can save their object using 'set_touch_handle'.
 * Also, they can restore that using 'get_touch_handle'.
 */

void set_touch_handle(struct i2c_client *client, void *h_touch)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	ts->h_touch = h_touch;
}

void *get_touch_handle(struct i2c_client *client)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	return ts->h_touch;
}

static struct bus_type touch_subsys = {
	.name = LGE_TOUCH_NAME,
	.dev_name = "lge_touch",
};

static struct device device_touch = {
	.id    = 0,
	.bus   = &touch_subsys,
};

/* send_uevent
 *
 * It will be used to send u-event to Android-framework.
 */
void send_uevent(struct device *dev, char *string[2])
{
	kobject_uevent_env(&device_touch.kobj, KOBJ_CHANGE, string);
	TOUCH_DEBUG(DEBUG_BASE_INFO, "uevent[%s]\n", string[0]);
}

/* send_uevent_lpwg
 *
 * It uses wake-lock in order to prevent entering the sleep-state,
 * during recognition or verification.
 */
#define VALID_LPWG_UEVENT_SIZE 4
static char *lpwg_uevent[VALID_LPWG_UEVENT_SIZE][2] = {
	{"TOUCH_GESTURE_WAKEUP=WAKEUP", NULL},
	{"TOUCH_GESTURE_WAKEUP=PASSWORD", NULL},
	{"TOUCH_GESTURE_WAKEUP=SIGNATURE", NULL},
	{"TOUCH_GESTURE_WAKEUP=SWIPE", NULL}
};

int send_uevent_lpwg(struct i2c_client *client, int type)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);

	wake_lock_timeout(&ts->lpwg_wake_lock, msecs_to_jiffies(3000));

	if (type > 0 && type <= VALID_LPWG_UEVENT_SIZE
			&& atomic_read(&ts->state.uevent_state)
			== UEVENT_IDLE) {
		if (type == LPWG_SWIPE_DOWN
				&& (atomic_read(&ts->state.power_state) == POWER_ON
					|| lockscreen_stat == 0)) {
			ts->pdata->swipe_pwr_ctrl_stage = WAIT_SWIPE_WAKEUP;
			TOUCH_INFO_MSG("drop LPWG_SWIPE_DOWN event (swipe_pwr_ctrl_stage = %d)\n",
					ts->pdata->swipe_pwr_ctrl_stage);
			return -1;
		}

		atomic_set(&ts->state.uevent_state, UEVENT_BUSY);
		send_uevent(&client->dev, lpwg_uevent[type-1]);
	}

	return 0;
}

/* touch_i2c_read / touch_i2c_write
 *
 * Developer can use these fuctions
 * to communicate with touch_device through I2C.
 */
int touch_i2c_read(struct i2c_client *client, u8 reg, int len, u8 *buf)
{
	int retry = 0;

	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf,
		},
	};

	for (retry = 0; retry < I2C_MAX_TRY; retry++) {
		if (i2c_transfer(client->adapter, msgs, 2) < 0) {
			if (printk_ratelimit())
				TOUCH_ERR_MSG("transfer error, "
						"retry (%d)times\n", retry + 1);
			msleep(20);
		} else
			break;
	}

	if (retry == I2C_MAX_TRY)
		return -EIO;

	return 0;
}

int touch_i2c_write(struct i2c_client *client, u8 reg, int len, u8 *buf)
{
	int retry = 0;

	unsigned char send_buf[len + 1];
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = client->flags,
			.len = len+1,
			.buf = send_buf,
		},
	};

	send_buf[0] = (unsigned char)reg;
	memcpy(&send_buf[1], buf, len);

	for (retry = 0; retry < I2C_MAX_TRY; retry++) {
		if (i2c_transfer(client->adapter, msgs, 1) < 0) {
			if (printk_ratelimit())
				TOUCH_ERR_MSG("transfer error, "
						"retry (%d)times\n", retry + 1);
			msleep(20);
		} else
			break;
	}

	if (retry == I2C_MAX_TRY)
		return -EIO;

	return 0;
}

int touch_i2c_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int retry = 0;

	unsigned char send_buf[2];
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = client->flags,
			.len = 2,
			.buf = send_buf,
		},
	};

	send_buf[0] = (unsigned char)reg;
	send_buf[1] = (unsigned char)data;

	for (retry = 0; retry < I2C_MAX_TRY; retry++) {
	if (i2c_transfer(client->adapter, msgs, 1) < 0) {
		if (printk_ratelimit())
				TOUCH_ERR_MSG("transfer error, retry (%d)times\n", retry + 1);
				msleep(20);
		} else
			break;
	}

	if (retry == I2C_MAX_TRY)
		return -EIO;

		return 0;
}


/* MACROs and functions for event_filter
 *
 */
#define f_sub(x, y)	(x > y ? x - y : y - x)
#define f_abs(x)	(x > 0 ? x : -x)
#define f_max(x, y)	(x > y ? x : y)
#define f_edge(x, max)	(x < 0 ? 0 : (x > max ? max : x))
#define f_cal(x, value) (x > 0 ? (x - value > 0 ? x - value : 0) \
		: (x + value < 0 ? x + value : 0))

static int get_pointer_index(const struct touch_data *data, int id)
{
	int i = 0;
	for (i = 0; i < data->total_num; i++) {
		if ((data->abs_data[i].id == id) && (data->id_mask & (1 << id)))
			return i;
	}
	return -EPERM;
}

/* Bouncing_filter
 *
 * remove ghost event.
 */
static enum filter_return_type bouncing_filter(struct i2c_client *client,
		u32 *report_id_mask)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	*report_id_mask = ts->ts_curr_data.id_mask;

	return FRT_REPORT;
}

/* Grip_filter
 *
 * remove un-intentional grip event
 *
 */
static enum filter_return_type grip_filter(struct i2c_client *client,
		u32 *report_id_mask)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);

	struct touch_data *curr_data = &ts->ts_curr_data;
	struct t_data *c_data;
	struct grip_filter_info *info = &ts->grip_filter;

	const struct touch_data *prev_data = &ts->ts_prev_data;
	const struct t_data *p_data;

	u32 id_mask_curr_only =
		(curr_data->id_mask ^ prev_data->id_mask) & curr_data->id_mask;
	u32 id_mask_prev_only =
		(curr_data->id_mask ^ prev_data->id_mask) & prev_data->id_mask;

	int i = 0, id = 0, index = -1;

	u32 edge = info->role->edge_region;
	u32 w_ratio = info->role->width_ratio;
	u32 max_d = info->role->max_delta;
	u32 max_x = ts->pdata->caps->max_x;

	for (i = 0 ; i < curr_data->total_num; i++) {
		c_data = &curr_data->abs_data[i];
		id = c_data->id;
		if (id < 0 || id >= MAX_FINGER)
			continue;

		if (info->data.grip_mask & (1 << id)) {
			index = get_pointer_index(prev_data, id);
			if (index  < 0 || index >= MAX_FINGER
					|| id < 0 || id >= MAX_FINGER)
				continue;

			p_data = &prev_data->abs_data[index];

			if ((c_data->raw_x >= edge
						&& c_data->raw_x <= max_x - edge
						&& c_data->width_major
						< c_data->width_minor * w_ratio)
					|| f_sub(c_data->raw_x,
						p_data->raw_x) > max_d) {
				info->data.grip_mask &= ~(1 << id);
			}
		}

		if (id_mask_curr_only & (1 << id)) {
			if (c_data->raw_x < edge
					|| c_data->raw_x > max_x - edge)
				info->data.grip_mask |= (1 << id);
		}

		if (id_mask_prev_only & (1 << id))
			info->data.grip_mask &= ~(1 << id);

		TOUCH_DEBUG(DEBUG_GRIP,
				"(%d) grip_mask[0x%x] wM[%d] wm[%d] "
				"x[%d] d_x[%d]\n",
				id, info->data.grip_mask,
				c_data->width_major, c_data->width_minor,
				c_data->raw_x,
				index >= 0 ? f_sub(c_data->raw_x,
					p_data->raw_x) : 0);
	}

	if (curr_data->total_num == 0)
		memset(&info->data, 0, sizeof(struct grip_filter_data));

	*report_id_mask = ts->ts_curr_data.id_mask & ~(info->data.grip_mask);

	return FRT_REPORT;
}

/* Accuracy_filter
 *
 * Improve the accuracy by using pressure.
 */
#define FILTER_START	1
#define FILTER_END	2

static enum filter_return_type accuracy_filter(struct i2c_client *client,
		u32 *report_id_mask)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);

	struct touch_data *curr_data = &ts->ts_curr_data;
	struct t_data *c_data;
	struct accuracy_filter_info *info = &ts->accuracy_filter;

	const struct touch_data *prev_data = &ts->ts_prev_data;
	const struct t_data *p_data;

	u32 id_mask_prev_only =
		(curr_data->id_mask ^ prev_data->id_mask) & prev_data->id_mask;

	int i = 0, id = -1, index = -1;

	u32 delta_x = 0, delta_y = 0, delta_max = 0;
	u32 delta_down_x = 0, delta_down_y = 0;
	u32 delta_z = 0;

	u32 np = info->role->min_pressure;
	u32 min_delta = info->role->min_delta;
	u32 ratio = info->role->curr_ratio;

	*report_id_mask = ts->ts_curr_data.id_mask;

	for (i = 0; i < curr_data->total_num; i++) {
		c_data = &curr_data->abs_data[i];
		id = c_data->id;

		if (!info->data[id].do_filter && c_data->pressure > np) {
			info->data[id].down_x = c_data->raw_x;
			info->data[id].down_y = c_data->raw_y;
			info->data[id].down_z = c_data->pressure;
			info->data[id].do_filter = FILTER_START;
			continue;
		}

		index = get_pointer_index(prev_data, id);
		if (index  < 0 || index >= MAX_FINGER
				|| id < 0 || id >= MAX_FINGER)
			continue;

		p_data = &prev_data->abs_data[index];

		delta_x = (u32)f_sub(p_data->raw_x, c_data->raw_x);
		delta_y = (u32)f_sub(p_data->raw_y, c_data->raw_y);
		delta_max = f_max(delta_x, delta_y);

		delta_down_x = (u32)f_sub(c_data->x, info->data[id].down_x);
		delta_down_y = (u32)f_sub(c_data->y, info->data[id].down_y);
		info->data[id].delta_pos = f_max(info->data[id].delta_pos,
				f_max(delta_down_x, delta_down_y));

		delta_z = (u32)f_sub(p_data->pressure, c_data->pressure);
		info->data[id].delta_z = f_max(info->data[id].delta_z,
				(u32)f_sub(info->data[id].down_z,
					c_data->pressure));

		if (info->data[id].do_filter == FILTER_START
				&& ((delta_z + min_delta < delta_max)
					|| (info->data[id].delta_z + min_delta
						< info->data[id].delta_pos))) {
			info->data[id].do_filter = FILTER_END;
			info->data[id].mod_x = p_data->x - p_data->raw_x;
			info->data[id].mod_y = p_data->y - p_data->raw_y;
		}

		if (info->data[id].do_filter == FILTER_START) {
			c_data->x = (c_data->x * ratio +
					(p_data->x * (128 - ratio))) >> 7;
			c_data->y = (c_data->y * ratio +
					(p_data->y * (128 - ratio))) >> 7;
		} else {
			c_data->x = (u16)f_abs((int)c_data->x
					+ info->data[id].mod_x);
			c_data->y = (u16)f_abs((int)c_data->y
					+ info->data[id].mod_y);

			if (info->data[id].mod_x != 0) {
				int delta_val = (delta_x >> 3) + 1;
				info->data[id].mod_x = delta_x > min_delta ?
					f_cal(info->data[id].mod_x, delta_val) :
					info->data[id].mod_x;
			}

			if (info->data[id].mod_y != 0) {
				int delta_val = (delta_y >> 3) + 1;
				info->data[id].mod_y = delta_y > min_delta ?
					f_cal(info->data[id].mod_y, delta_val) :
					info->data[id].mod_y;
			}
		}

		TOUCH_DEBUG(DEBUG_ACCURACY,
				"AccuracyFilter: <%d> old[%4d,%4d] new[%4d,%4d]"
				" p[%3d] d[%3d,%3d] d_z[%3d,%3d] mod[%3d,%3d] "
				"do_filter[%d]\n",
				id, c_data->raw_x, c_data->raw_y, c_data->x,
				c_data->y, c_data->pressure, delta_max,
				info->data[id].delta_pos, delta_z,
				info->data[id].delta_z, info->data[id].mod_x,
				info->data[id].mod_y, info->data[id].do_filter);

	}

	for (i = 0; id_mask_prev_only && i < MAX_FINGER ; i++) {
		if (id_mask_prev_only & (1 << i))
			memset(&info->data[i], 0,
					sizeof(struct accuracy_filter_data));
		id_mask_prev_only &= ~(1 << i);
	}

	return FRT_REPORT;
}


/* Jitter_filter
 *
 * Reduce the jitter by using delta-position value.
 */
static enum filter_return_type jitter_filter(struct i2c_client *client,
		u32 *report_id_mask)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);

	struct touch_data *curr_data = &ts->ts_curr_data;
	struct t_data *c_data;
	struct jitter_filter_info *info = &ts->jitter_filter;

	const struct touch_data *prev_data = &ts->ts_prev_data;
	const struct t_data *p_data;
	const struct touch_data *report_data = &ts->ts_report_data;
	const struct t_data *r_data;

	u32 id_mask_prev_only =
		(curr_data->id_mask ^ prev_data->id_mask) & prev_data->id_mask;

	u16 i, id = 0;
	int delta_x, delta_y;
	int index = -1;

	int f_jitter = 0;
	u32 id_mask = 0;

	u32 ratio = info->role->curr_ratio;

	*report_id_mask = ts->ts_curr_data.id_mask;

	for (i = 0; i < curr_data->total_num; i++) {
		c_data = &curr_data->abs_data[i];
		id = c_data->id;
		index = get_pointer_index(prev_data, id);
		if (index  < 0 || index >= MAX_FINGER
				|| id < 0 || id >= MAX_FINGER)
			continue;

		p_data = &prev_data->abs_data[index];
		r_data = &report_data->abs_data[index];

		f_jitter = (128 - ratio) * c_data->width_major;

		c_data->x = (c_data->x  * ratio
				+ p_data->x * (128 - ratio)) >> 7;
		c_data->y = (c_data->y  * ratio
				+ p_data->y * (128 - ratio)) >> 7;

		delta_x = (int)f_sub(c_data->x, r_data->x);
		delta_y = (int)f_sub(c_data->y, r_data->y);

		info->data[id].delta_x = delta_x * ratio
			+ ((info->data[id].delta_x * (128 - ratio)) >> 7);
		info->data[id].delta_y = delta_y * ratio
			+ ((info->data[id].delta_y * (128 - ratio)) >> 7);

		if (info->data[id].delta_x > f_jitter
				|| info->data[id].delta_y > f_jitter)
			id_mask |= 1 << id;

		TOUCH_DEBUG(DEBUG_JITTER,
				"JitterFilter: <%d> old[%4d,%4d] curr[%4d,%4d]"
				"prev[%4d,%4d] d[%4d,%4d] w[%4d] f_j[%d]\n",
				id, c_data->raw_x, c_data->raw_y, c_data->x,
				c_data->y, p_data->x, p_data->y,
				info->data[id].delta_x, info->data[id].delta_y,
				c_data->width_major, f_jitter);
	}

	for (i = 0; id_mask_prev_only && i < MAX_FINGER ; i++) {
		if (id_mask_prev_only & (1 << i))
			memset(&info->data[i], 0,
					sizeof(struct jitter_filter_data));
		id_mask_prev_only &= ~(1 << i);
	}

	if (id_mask || (prev_data->id_mask != curr_data->id_mask))
		return FRT_REPORT;
	else
		return FRT_IGNORE;
}

/* Quickcover_filter
 *
 * Set touch enable area when Quick cover status is set.
 *
 */
static enum filter_return_type quickcover_filter(struct i2c_client *client,
		u32 *report_id_mask)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);

	struct touch_data *curr_data = &ts->ts_curr_data;
	struct t_data *c_data;
	struct quickcover_filter_info *info = &ts->quickcover_filter;

	const struct touch_data *prev_data = &ts->ts_prev_data;
	const struct t_data *p_data;

	int i = 0, id = 0, index = -1;

	u32 id_mask_prev_only =
		(curr_data->id_mask ^ prev_data->id_mask) & prev_data->id_mask;

	u32 X1 = info->role->X1;
	u32 X2 = info->role->X2;
	u32 Y1 = info->role->Y1;
	u32 Y2 = info->role->Y2;

	for (i = 0 ; i < curr_data->total_num; i++) {
		c_data = &curr_data->abs_data[i];
		id = c_data->id;
		if (id < 0 || id >= MAX_FINGER)
			continue;

		if (quick_cover_status == QUICKCOVER_OPEN)
			continue;

		if (info->data.quickcover_mask & (1 << id)) {
			index = get_pointer_index(prev_data, id);
			if (index  < 0 || index >= MAX_FINGER
					|| id < 0 || id >= MAX_FINGER)
				continue;

			p_data = &prev_data->abs_data[index];

			if ((c_data->raw_x > X1 && c_data->raw_x < X2)
					&& (c_data->raw_y > Y1
						&& c_data->raw_y < Y2)) {
				info->data.quickcover_mask &= ~(1 << id);
			}
		}

		if (c_data->raw_x < X1 || c_data->raw_x > X2
				|| c_data->raw_y < Y1 || c_data->raw_y > Y2)
			info->data.quickcover_mask |= (1 << id);

		if (id_mask_prev_only & (1 << id))
			info->data.quickcover_mask &= ~(1 << id);

		TOUCH_DEBUG(DEBUG_QUICKCOVER,
				"(%d) quickcover_mask[0x%x] x[%d] y[%d] \n",
				id, info->data.quickcover_mask,
				c_data->raw_x, c_data->raw_y);
	}

	if (curr_data->total_num == 0)
		memset(&info->data, 0, sizeof(struct quickcover_filter_data));

	*report_id_mask = ts->ts_curr_data.id_mask
		& ~(info->data.quickcover_mask);

	return FRT_REPORT;
}

/* id_checker
 *
 * if 'id' is not valid, it will be changed in 'report_event'.
 * (ex. '0' should be assigned to first-finger)
 */
static void id_checker(struct touch_data *c_data,
		const struct touch_data *p_data, struct filter_data *f_data)
{
	int i = 0;

	if (p_data->total_num == 0 && c_data->total_num != 0)
		memset(f_data, 0, sizeof(struct filter_data));

	for (i = 0; i < c_data->total_num; i++) {
		if (!(c_data->report_id_mask & (1 << c_data->abs_data[i].id))
				|| c_data->abs_data[i].id < 0
				|| c_data->abs_data[i].id >= MAX_FINGER)
			continue;

		if (!f_data->id_checklist[c_data->abs_data[i].id]) {
			f_data->index++;
			f_data->id_checklist[c_data->abs_data[i].id]
				= f_data->index;
		}
	}
}

/* core_filter
 *
 * return -1 if the event should be ignored. (0 if not)
 */
static int core_filter(struct lge_touch_data *ts)
{
	struct filter_func *tmp;
	struct list_head *pos;

	enum filter_return_type ret = FRT_REPORT;
	u32 tmp_mask = 0;

	list_for_each(pos, &ts->filter_head.list) {
		tmp = list_entry(pos, struct filter_func, list);
		ret &= tmp->filter(ts->client, &tmp_mask);
		ts->ts_curr_data.report_id_mask &= tmp_mask;
		TOUCH_DEBUG(DEBUG_FILTER_RESULT,
				"<%s> ret[%d] id_mask[0x%x] [0x%x -> 0x%x]\n",
				tmp->name, ret, ts->ts_curr_data.report_id_mask,
				ts->ts_curr_data.id_mask, tmp_mask);
	}

	id_checker(&ts->ts_curr_data, &ts->ts_prev_data, &ts->f_data);

	if (ret == FRT_IGNORE)
		return -1;

	return 0;
}

/* report_key
 *
 * report H/W key event
 */
static inline void report_key(struct input_dev *dev,
		unsigned int code, int value)
{
	input_report_key(dev, code, value);
	TOUCH_DEBUG(DEBUG_BUTTON | DEBUG_BASE_INFO , "KEY[%d] is %s(%d)\n",
			code, value ? "pressed" : "released", value);
}

/* key_event
 *
 * Key event processing (only for H/W key)
 */
static int key_event(struct lge_touch_data *ts)
{
	struct touch_data *curr_data = &ts->ts_curr_data;
	struct touch_data *prev_data = &ts->ts_prev_data;

	if (curr_data->total_num > 0) {
		// button release process
		if (prev_data->button_data.key_code != 0
				&& (prev_data->button_data.state
					== BUTTON_PRESSED)) {
			report_key(ts->input_dev,
					prev_data->button_data.key_code,
					BUTTON_CANCLED);
			curr_data->button_data.state = BUTTON_CANCLED;
		}
	} else {
		// case: curr. keycode is different from prev.
		if (curr_data->button_data.key_code
				!= prev_data->button_data.key_code) {
			if (prev_data->button_data.state != BUTTON_RELEASED)
				report_key(ts->input_dev,
						prev_data->button_data.key_code,
						BUTTON_RELEASED);
			if (curr_data->button_data.state == BUTTON_PRESSED)
				report_key(ts->input_dev,
						curr_data->button_data.key_code,
						BUTTON_PRESSED);
		} else {	// case: curr. keycode is same as prev.
			if (prev_data->button_data.state != BUTTON_CANCLED) {
				if (curr_data->button_data.state
						!= prev_data->button_data.state)
					report_key(ts->input_dev,
							curr_data->button_data.key_code,
							curr_data->button_data.state);
			} else {
				if (curr_data->button_data.state
						== BUTTON_PRESSED)
					report_key(ts->input_dev,
							curr_data->button_data.key_code,
							BUTTON_PRESSED);
			}
		}
	}

	TOUCH_DEBUG(DEBUG_BUTTON, "Curr_button: code[%d] state[%d], "
			"Prev_button: code[%d] state[%d]\n",
			curr_data->button_data.key_code,
			curr_data->button_data.state,
			prev_data->button_data.key_code,
			prev_data->button_data.state);

	return 0;
}


/* report_event
 *
 * report abs event.
 * support : Both Protocol-A and Protocol-B
 */
static int report_event(const struct lge_touch_data *ts)
{
	int i = 0;
	u16 new_id = 0;
	u32 new_id_mask = 0;
	char log_buf[50] = {0};
	int rc = 0;

	// press
	for (i = 0; i < ts->ts_curr_data.total_num; i++) {
		if (!(ts->ts_curr_data.report_id_mask &
					(1 << ts->ts_curr_data.abs_data[i].id))
				|| ts->ts_curr_data.abs_data[i].id < 0 ||
				ts->ts_curr_data.abs_data[i].id >= MAX_FINGER)
			continue;

		new_id = ts->f_data.id_checklist[ts->ts_curr_data.abs_data[i].id] - 1;
		new_id_mask |= 1 << new_id;

		if (ts->pdata->role->protocol_type == MT_PROTOCOL_B)
			input_mt_slot(ts->input_dev, new_id);

		input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, new_id);
		input_report_abs(ts->input_dev, ABS_MT_POSITION_X,
				ts->ts_curr_data.abs_data[i].x);
		input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,
				ts->ts_curr_data.abs_data[i].y);
		input_report_abs(ts->input_dev, ABS_MT_PRESSURE,
				ts->ts_curr_data.abs_data[i].pressure);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR,
				ts->ts_curr_data.abs_data[i].width_major);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MINOR,
				ts->ts_curr_data.abs_data[i].width_minor);
		input_report_abs(ts->input_dev, ABS_MT_ORIENTATION,
				ts->ts_curr_data.abs_data[i].orientation);

		TOUCH_DEBUG(DEBUG_ABS, "<%d:%d> pos[%4d,%4d] w_m[%2d] w_n[%2d] "
				"o[%2d] p[%3d]\n",
				ts->ts_curr_data.abs_data[i].id, new_id,
				ts->ts_curr_data.abs_data[i].x,
				ts->ts_curr_data.abs_data[i].y,
				ts->ts_curr_data.abs_data[i].width_major,
				ts->ts_curr_data.abs_data[i].width_minor,
				ts->ts_curr_data.abs_data[i].orientation,
				ts->ts_curr_data.abs_data[i].pressure);

		if (is_Sensing) {
			sprintf(log_buf, "<%d> x[%4d] y[%4d] z[%3d] %s\n",
					new_id,
					ts->ts_curr_data.abs_data[i].x,
					ts->ts_curr_data.abs_data[i].y,
					ts->ts_curr_data.abs_data[i].pressure,
					"DOWN");
			rc = write_file(SENSING_TEST_PATH, log_buf);
			if (rc < 0)
				TOUCH_DEBUG(DEBUG_BASE_INFO, "%s : write log failed\n", __func__);
		}

		if ((ts->ts_curr_data.report_id_mask &
					(1 << ts->ts_curr_data.abs_data[i].id))
				&& !(ts->ts_prev_data.report_id_mask &
					(1 << ts->ts_curr_data.abs_data[i].id))) {
			++ts->pdata->touch_count_num;

			if (lockscreen_stat && knock_mode) {
				TOUCH_INFO_MSG("%d finger pressed: <%d> "
						"x[xxxx] y[xxxx] z[xxx]\n",
						ts->pdata->touch_count_num,
						new_id);
			} else {
				TOUCH_INFO_MSG("%d finger pressed: <%d> x[%4d] "
						"y[%4d] z[%3d]\n",
						ts->pdata->touch_count_num,
						new_id,
						ts->ts_curr_data.abs_data[i].x,
						ts->ts_curr_data.abs_data[i].y,
						ts->ts_curr_data.abs_data[i].pressure);
			}
		}

		if (ts->pdata->role->protocol_type == MT_PROTOCOL_A)
			input_mt_sync(ts->input_dev);
	}

	// release
	if (ts->pdata->role->protocol_type == MT_PROTOCOL_A) {
		if (ts->ts_curr_data.total_num == 0)
			input_mt_sync(ts->input_dev);
	} else {
		for (i = 0; i < ts->ts_prev_data.total_num; i++) {
			if (!(ts->ts_curr_data.report_id_mask &
						(1 << ts->ts_prev_data.abs_data[i].id))
					&& (ts->ts_prev_data.report_id_mask &
						(1 << ts->ts_prev_data.abs_data[i].id))
					&& ts->ts_curr_data.abs_data[i].id >= 0
					&& ts->ts_curr_data.abs_data[i].id < MAX_FINGER) {
				new_id = ts->f_data.id_checklist[ts->ts_prev_data.abs_data[i].id] - 1;
				input_mt_slot(ts->input_dev, new_id);
				input_report_abs(ts->input_dev,
						ABS_MT_TRACKING_ID, -1);
				ts->pdata->touch_count_num--;
				TOUCH_DEBUG(DEBUG_ABS, "<%d:%d> released\n",
						ts->ts_prev_data.abs_data[i].id,
						new_id);

				if (lockscreen_stat && knock_mode) {
					TOUCH_INFO_MSG("touch_release[ ]: <%d> "
							"x[xxxx] y[xxxx]\n",
							new_id);
				} else {
					TOUCH_INFO_MSG("touch_release[ ]: <%d> "
							"x[%4d] y[%4d]\n",
							new_id,
							ts->ts_prev_data.abs_data[i].x,
							ts->ts_prev_data.abs_data[i].y);
				}

				if (is_Sensing) {
					sprintf(log_buf, "<%d> x[%4d] y[%4d] %s\n",
							new_id,
							ts->ts_prev_data.abs_data[i].x,
							ts->ts_prev_data.abs_data[i].y,
							"          UP");
					rc = write_file(SENSING_TEST_PATH, log_buf);
					if (rc < 0)
						TOUCH_DEBUG(DEBUG_BASE_INFO, "%s : write log failed\n", __func__);


				}
			}
		}
	}

	input_sync(ts->input_dev);
	/*
	   if (ts->ts_curr_data.id_mask != ts->ts_prev_data.id_mask
	   || ts->ts_curr_data.report_id_mask
	   != ts->ts_prev_data.report_id_mask)
	   TOUCH_DEBUG(DEBUG_ABS | DEBUG_BASE_INFO,
	   "report[0x%x] : curr - id_mask[0x%x/0x%x] total_num[%d]
	   / prev - id_mask[0x%x/0x%x] total_num[%d]\n",
	   new_id_mask, ts->ts_curr_data.id_mask,
	   ts->ts_curr_data.report_id_mask, ts->ts_curr_data.total_num,
	   ts->ts_prev_data.id_mask, ts->ts_prev_data.report_id_mask,
	   ts->ts_prev_data.total_num);
	 */

	return 0;
}


/* release_all_touch_event
 *
 * before turn off the power, all events should be released.
 */
void release_all_touch_event(struct lge_touch_data *ts)
{
	if (atomic_read(&ts->state.rebase_state) == REBASE_DOING) {
		TOUCH_DEBUG(DEBUG_BASE_INFO,
				"rebase work .\n");
	}

	memset(&ts->ts_curr_data, 0, sizeof(struct touch_data));
	if (ts->ts_prev_data.total_num) {
		report_event(ts);
		ts->ts_prev_data.total_num = 0;
	}
	if (ts->pdata->caps->button_support)
		key_event(ts);

	ts->pdata->touch_count_num = 0;
}


/* power_control
 *
 * 'power_state' can has only 'ON' or 'OFF'. (not 'SLEEP' or 'WAKE')
 * During firmware upgrade, power state will not be changed.
 */
static int power_control(struct lge_touch_data *ts, int on_off)
{
	if (atomic_read(&ts->state.upgrade_state) == UPGRADE_START) {
		TOUCH_DEBUG(DEBUG_BASE_INFO,
				"'Firmware-upgrade' is not finished,"
				"so power cannot be changed.\n");
		return 0;
	} else if (atomic_read(&ts->state.crack_test_state)
			== CRACK_TEST_START) {
		TOUCH_INFO_MSG("Crack Test in not finished, "
				"so power cannot be changed.\n");
		return 0;
	}
	/* To ignore the probe time */
	if (ts->input_dev != NULL)
		release_all_touch_event(ts);

	if (atomic_read(&ts->state.power_state) != on_off) {
		DO_IF(touch_device_func->power(ts->client, on_off) != 0, error);
		atomic_set(&ts->state.power_state, on_off);
	}

	if (atomic_read(&ts->state.power_state) == POWER_OFF)
		atomic_set(&ts->state.device_init_state, INIT_NONE);

	return 0;
error:
	TOUCH_ERR_MSG("%s, %d : Power control failed\n", __func__, __LINE__);
	return -1;
}


/* interrupt_control
 *
 * It cannot defend 'critical-section', perfectly,
 * but the possibility of an error occuring (race-condition) is very low.
 * (so, it is not a big problem, now. - I think.)
 *
 * It only can prevent execute twice, either 'enable_irq' or 'disable_irq'.
 */
static int interrupt_control(struct lge_touch_data *ts, int on_off)
{
	TOUCH_DEBUG(DEBUG_BASE_INFO, "interrupt_state[%d -> %d]\n",
			atomic_read(&ts->state.interrupt_state), on_off);

	if (atomic_read(&ts->state.interrupt_state) != on_off) {
		atomic_set(&ts->state.interrupt_state, on_off);

		if (on_off) {
			touch_enable_irq(ts->client->irq);
		} else {
			TOUCH_INFO_MSG("%s : disable irq nosync\n", __func__);
			touch_disable_irq(ts->client->irq);
		}

		if (ts->pdata->role->wake_up_by_touch && !is_probe) {
			TOUCH_INFO_MSG("%s : enable_irq_wake once\n", __func__);
			enable_irq_wake(ts->client->irq);
		}
	}

	TOUCH_DEBUG(DEBUG_BASE_INFO, "interrupt_state[%d]\n", on_off);
	return 0;
}


/* safety_reset
 *
 * 1. release all events if it needs to.
 * 2. turn off the power.
 * 3. sleep (reset_delay)ms.
 * 4. turn on the power
 * 5. sleep (booting_delay)ms.
 *
 * After 'safety_reset', 'touch_ic_init' should be executed.
 */
static void safety_reset(struct lge_touch_data *ts)
{
	u32 ret = 0;
	int prev_power_state = atomic_read(&ts->state.power_state);
	TOUCH_TRACE();

	TOUCH_INFO_MSG("safety_reset start\n");
	switch(ts->pdata->pwr->reset_control)
	{
	case SOFT_RESET:
		TOUCH_INFO_MSG("SOFT_RESET\n");
		release_all_touch_event(ts);
		DO_IF(touch_device_func->ic_ctrl(ts->client, IC_CTRL_RESET, 0x01, &ret) != 0, error);
		msleep(ts->pdata->role->reset_delay);
		break;
	case PIN_RESET:
		TOUCH_INFO_MSG("PIN_RESET\n");
		interrupt_control(ts, INTERRUPT_DISABLE);
		if (ts->pdata->reset_pin > 0) {
			release_all_touch_event(ts);
			gpio_direction_output(ts->pdata->reset_pin, 0);
			msleep(ts->pdata->role->reset_delay);
			gpio_direction_output(ts->pdata->reset_pin, 1);
		}
		msleep(ts->pdata->role->booting_delay);
		interrupt_control(ts, INTERRUPT_ENABLE);
		break;
	case VDD_RESET:
		TOUCH_INFO_MSG("VDD_RESET\n");
		DO_SAFE(power_control(ts, POWER_OFF), error);
		msleep(ts->pdata->role->reset_delay);
		DO_SAFE(power_control(ts, POWER_ON), error);
		msleep(ts->pdata->role->booting_delay);

		if (prev_power_state != POWER_ON)
			DO_SAFE(power_control(ts, prev_power_state), error);

		break;
	default:
		break;
	}
	TOUCH_INFO_MSG("safety_reset end\n");

	return;
error:
	/* TO_DO : error handling, if it needs */
	TOUCH_ERR_MSG("%s, %d : safety reset failed\n", __func__, __LINE__);
	return;
}

static int get_panel_id(struct lge_touch_data *ts)
{
	int panel_id = 0xFF;
	int ret, value = 0;
	static bool panel_id_init = false;

	if (panel_id_init && ts->pdata->panel_id) {
		TOUCH_DEBUG(DEBUG_BASE_INFO, "panel id : %d", ts->pdata->panel_id);
		return ts->pdata->panel_id;
	}

	if (!panel_id_init) {
		if (ts->pdata->maker_id_pin && gpio_is_valid(ts->pdata->maker_id_pin)) {
			ret = gpio_request(ts->pdata->maker_id_pin, "touch_id");
			if (ret < 0) {
				TOUCH_ERR_MSG("FAIL: touch_id gpio_request = %d\n", ret);
				goto Exit;
			} else {
				gpio_direction_input(ts->pdata->maker_id_pin);
			}
		} else {
			TOUCH_DEBUG(DEBUG_BASE_INFO, "maker_id_gpio is invalid\n");
		}
		panel_id_init = true;
	}

	if (ts->pdata->maker_id_pin && gpio_is_valid(ts->pdata->maker_id_pin)) {
		value = gpio_get_value(ts->pdata->maker_id_pin);
		TOUCH_DEBUG(DEBUG_BASE_INFO, "MAKER_ID : %s\n", value ? "High" : "Low");
		panel_id = (value & 0x1);
	}

	panel_id_init = true;
	ts->pdata->panel_id = panel_id;
	TOUCH_DEBUG(DEBUG_BASE_INFO, "Touch panel id : %d", ts->pdata->panel_id);

	return panel_id;

Exit :
	TOUCH_ERR_MSG("%s FAIL \n", __func__);
	return 0xFF;
}


static int get_fw_pid_addr(struct lge_touch_data *ts, int panel_id)
{
	if (ts->pdata->fw_pid_addr)
		return 0;

	if (!ts->pdata->fw_pid_addr_list[panel_id])
		ts->pdata->fw_pid_addr = ts->pdata->fw_pid_addr_list[0];
	else
		ts->pdata->fw_pid_addr = ts->pdata->fw_pid_addr_list[panel_id + ts->pdata->role->bootloader_type];

	return 1;

}

static int get_inbuilt_fw_path(struct lge_touch_data *ts, int panel_id)
{
	if (ts->pdata->inbuilt_fw_name)
		return 0;

	if (ts->pdata->inbuilt_fw_name_list[panel_id] == NULL)
		ts->pdata->inbuilt_fw_name = ts->pdata->inbuilt_fw_name_list[0];
	else
		ts->pdata->inbuilt_fw_name = ts->pdata->inbuilt_fw_name_list[panel_id + ts->pdata->role->bootloader_type];

	return 1;
}

static int get_fw_ver_addr(struct lge_touch_data *ts, int panel_id)
{
	if (ts->pdata->fw_ver_addr)
		return 0;

	if (!ts->pdata->fw_ver_addr_list[panel_id])
		ts->pdata->fw_ver_addr = ts->pdata->fw_ver_addr_list[0];
	else
		ts->pdata->fw_ver_addr = ts->pdata->fw_ver_addr_list[panel_id + ts->pdata->role->bootloader_type];

	return 1;
}

static void Select_Firmware(struct lge_touch_data *ts, int bl_type)
{
	int panel_id = 0;

	if(bl_type < 0)
		TOUCH_DEBUG(DEBUG_BASE_INFO, "Get boot loader tpye failed\n");
	else if( bl_type == NO_NEED_BL) {
		TOUCH_DEBUG(DEBUG_BASE_INFO, "This should be SIC\n");
	}
	else
		TOUCH_DEBUG(DEBUG_BASE_INFO, "boot loader tpye : %s\n", bl_type ? "Ver 7.2" : "Ver 6.0");

	TOUCH_DEBUG(DEBUG_BASE_INFO, "maker_id_pin : %d\n", ts->pdata->maker_id_pin);
	if (ts->pdata->maker_id_pin > 0) {
		panel_id = get_panel_id(ts);
		if (panel_id == 0xFF) {
			TOUCH_DEBUG(DEBUG_BASE_INFO, "Fail to get panel id \n");
			ts->pdata->inbuilt_fw_name = ts->pdata->inbuilt_fw_name_list[0];
			ts->pdata->fw_ver_addr = ts->pdata->fw_ver_addr_list[0];
			ts->pdata->fw_pid_addr = ts->pdata->fw_pid_addr_list[0];
		} else {
			TOUCH_DEBUG(DEBUG_BASE_INFO, "Success to get panel id \n");
			get_inbuilt_fw_path(ts, panel_id);
			get_fw_ver_addr(ts, panel_id);
			get_fw_pid_addr(ts, panel_id);
		}
	} else {
		ts->pdata->inbuilt_fw_name = ts->pdata->inbuilt_fw_name_list[0];
		ts->pdata->fw_ver_addr = ts->pdata->fw_ver_addr_list[0];
		ts->pdata->fw_pid_addr = ts->pdata->fw_pid_addr_list[0];
	}
	TOUCH_DEBUG(DEBUG_BASE_INFO, "fw_image : %s \n", ts->pdata->inbuilt_fw_name);
	TOUCH_DEBUG(DEBUG_BASE_INFO, "fw_ver_addr : 0x%x\n", ts->pdata->fw_ver_addr);
	TOUCH_DEBUG(DEBUG_BASE_INFO, "fw_PID_addr : 0x%x\n", ts->pdata->fw_pid_addr);
}

/* touch_ic_init
 *
 * initialize the device_IC and variables.
 *
 * If you modify this function, please check the mutex.
 * Mutex should be unlocked when the thread exits this function.
 */
int touch_ic_init(struct lge_touch_data *ts, int is_error)
{
	TOUCH_TRACE();

	TOUCH_INFO_MSG("%s : value = %d\n", __func__, is_error);

	DO_IF(touch_device_func->init(ts->client) != 0, error);

	current_thermal_mode = touch_thermal_status;
	TOUCH_INFO_MSG("current_thermal_mode = %s\n",
			current_thermal_mode ? "HIGH" : "LOW");

	if (current_thermal_mode) {
		queue_delayed_work(touch_wq, &ts_data->work_thermal,
				msecs_to_jiffies(0));
	}

	memset(&ts->ts_curr_data, 0, sizeof(struct touch_data));
	memset(&ts->ts_prev_data, 0, sizeof(struct touch_data));
	memset(&ts->ts_report_data, 0, sizeof(struct touch_data));

	memset(&ts->bouncing_filter.data, 0,
			sizeof(struct bouncing_filter_data));
	memset(&ts->grip_filter.data, 0, sizeof(struct grip_filter_data));
	memset(&ts->accuracy_filter.data, 0,
			sizeof(struct accuracy_filter_data));
	memset(&ts->jitter_filter.data, 0, sizeof(struct jitter_filter_data));
	memset(&ts->quickcover_filter.data, 0,
			sizeof(struct quickcover_filter_data));

	memset(&ts->f_data, 0, sizeof(struct filter_data));

	memset(t_ex_debug, 0, sizeof(t_ex_debug));

	if (ts->pdata->role->ghost_detection->check_enable.ghost_detection_enable) {
		ghost_detection = false;
		ghost_detection_count = 0;
		ts_rebase_count = 0 ;
		do_gettimeofday(&t_ex_debug[EX_INIT]);
	}

	atomic_set(&ts->state.device_init_state, INIT_DONE);
	return 0;

error:
	TOUCH_ERR_MSG("%s, %d : IC init failed\n", __func__, __LINE__);

	if (!is_error) {
		safety_reset(ts);
		return touch_ic_init(ts, 1);
	} else {
		power_control(ts, POWER_OFF);
		return -1;
	}
}


/* touch_init_func
 *
 * In order to reduce the booting-time,
 * we used delayed_work_queue instead of msleep or mdelay.
 */
static void touch_init_func(struct work_struct *work_init)
{
	struct lge_touch_data *ts = container_of(to_delayed_work(work_init),
			struct lge_touch_data, work_init);

	TOUCH_TRACE();
	mutex_lock(&ts->pdata->thread_lock);
	TOUCH_INFO_MSG("%s : Start-up init via resume\n", __func__);

	DO_SAFE(touch_ic_init(ts, 0), error);

	TOUCH_INFO_MSG("%s : End-up init via resume\n", __func__);
	mutex_unlock(&ts->pdata->thread_lock);
	return;
error:
	/* TO_DO : error handling, if it needs */
	mutex_unlock(&ts->pdata->thread_lock);
	TOUCH_ERR_MSG("%s : touch ic init fail\n", __func__);
	return;
}

#define ts_ghost_value	(ts->pdata->role->ghost_detection)
#define get_time_interval(a, b) a >= b ? a - b : 1000000 + a - b
struct t_data ts_prev_finger_press_data;

static bool is_valid_ghost_jitter(struct lge_touch_data *ts,
		struct t_data prev_data, struct t_data curr_data)
{
	int ret = 0;
	ret = (f_sub(prev_data.x, curr_data.x) <= ts_ghost_value->jitter_value
			&& f_sub(prev_data.y, curr_data.y)
			<= ts_ghost_value->jitter_value);
	return ret;
}

bool chk_time_interval(struct timeval t_aft, struct timeval t_bef, int t_val)
{
	int interval = t_val * 1000;
	if (t_aft.tv_sec - t_bef.tv_sec == 0) {
		if ((get_time_interval(t_aft.tv_usec, t_bef.tv_usec)) <= interval)
			return true;
	} else if (t_aft.tv_sec - t_bef.tv_sec == 1) {
		if (t_aft.tv_usec + 1000000 - t_bef.tv_usec <= interval)
			return true;
	}

	return false;
}

int rebase_ic(struct lge_touch_data *ts)
{
	u32 ret = 0;

	if (atomic_read(&ts->state.rebase_state) == REBASE_DOING) {
		TOUCH_DEBUG(DEBUG_BASE_INFO, "REBASE_DOING .\n");
		return NO_ACTION;
	} else {
		atomic_set(&ts->state.rebase_state, REBASE_DOING);
	}

	ghost_detection = false;
	ghost_detection_count = 0;
	button_press_count = 0;
	memset(&ts_prev_finger_press_data, 0x0,
			sizeof(ts_prev_finger_press_data));
	ts_rebase_count++;

	if (ts_ghost_value->check_enable.rebase_repetition_chk) {
		if (ts_rebase_count == 1) {
			do_gettimeofday(&t_ex_debug[EX_FIRST_GHOST_DETECT]);

			if ((t_ex_debug[EX_FIRST_GHOST_DETECT].tv_sec -
						t_ex_debug[EX_INIT].tv_sec) <=
					ts_ghost_value->rebase_since_init) {
				ts_rebase_count = 0;
				TOUCH_DEBUG(DEBUG_BASE_INFO, "need to init .\n");
				atomic_set(&ts->state.rebase_state, REBASE_DONE);
				return NEED_TO_INIT;
			}
		} else {
			do_gettimeofday(&t_ex_debug[EX_SECOND_GHOST_DETECT]);

			if (((t_ex_debug[EX_SECOND_GHOST_DETECT].tv_sec
							- t_ex_debug[EX_FIRST_GHOST_DETECT].tv_sec) <=
						ts_ghost_value->rebase_since_rebase)) {
				TOUCH_DEBUG(DEBUG_BASE_INFO,
						"need to init(%d)\n",
						ts_rebase_count);
				ts_rebase_count = 0;
				atomic_set(&ts->state.rebase_state,
						REBASE_DONE);
				return NEED_TO_INIT;
			} else {
				ts_rebase_count = 1;
				memcpy(&t_ex_debug[EX_FIRST_GHOST_DETECT],
						&t_ex_debug[EX_SECOND_GHOST_DETECT],
						sizeof(struct timeval));
			}
		}
	}

	release_all_touch_event(ts);

	memset(&ts->ts_prev_data, 0, sizeof(struct touch_data));
	DO_IF(touch_device_func->ic_ctrl(ts->client, IC_CTRL_BASELINE_REBASE, FORCE_CAL, &ret) != 0, error);

	TOUCH_DEBUG(DEBUG_BASE_INFO, "rebase done.\n");
	atomic_set(&ts->state.rebase_state, REBASE_DONE);


	return DONE_REBASE;

error:
	TOUCH_ERR_MSG("rebase fail .\n");
	atomic_set(&ts->state.rebase_state, REBASE_DONE);
	return ERROR_CASE;
}

int ghost_detect_solution(struct lge_touch_data *ts)
{
	int cnt = 0, id = 0;
	u32 ret = 0;

	/* incoming call check */
	if (ts_ghost_value->check_enable.incoming_call_chk
			&& atomic_read(&ts->state.incoming_call_state) == INCOMING_CALL_RINGING
			&& ts->ts_curr_data.total_num) {
		TOUCH_INFO_MSG("call state, need to rebase .\n");
		goto out_need_to_rebase;
	}

	if (ts_ghost_value->check_enable.first_finger_chk
			&& (!t_ex_debug[EX_FIRST_INT].tv_usec)) {
		do_gettimeofday(&t_ex_debug[EX_FIRST_INT]);
		if (chk_time_interval(t_ex_debug[EX_FIRST_INT],
					t_ex_debug[EX_INIT],
					ts_ghost_value->first_finger_time)) {
			for (cnt = 0; cnt < ts->pdata->caps->max_id; cnt++) {
				if (ts->ts_curr_data.report_id_mask & (1 << cnt)) {
					TOUCH_DEBUG(DEBUG_BASE_INFO,
							"first input check .\n");
					ghost_detection = true;
				}
			}
		}
	}

	/* z-value check*/
	if (ts_ghost_value->check_enable.pressure_zero_chk
			&& ts_ghost_value->pressure_zero) {
		TOUCH_DEBUG(DEBUG_BASE_INFO, "z value is zero .\n");
		ghost_detection = true;
		ts_ghost_value->pressure_zero = false;
	}

	/* reduce coupling , OTG */
	if (ts_ghost_value->check_enable.ta_debouncing_chk
			&& touch_ta_status) {
		if ((ts->ts_curr_data.total_num >=
					ts_ghost_value->ta_debouncing_finger_num)
				&& (ta_debouncing_count <
					ts_ghost_value->ta_debouncing_cnt)) {
			ta_debouncing_count++;
			TOUCH_DEBUG(DEBUG_BASE_INFO, "debounce .\n");
			goto out_need_to_debounce;
		} else if (ts->ts_curr_data.total_num <
				ts_ghost_value->ta_debouncing_finger_num) {
			ta_debouncing_count = 0;
		} else ;
	}

	if (ts->ts_curr_data.total_num) {
		if (ts->ts_prev_data.total_num < ts->ts_curr_data.total_num) {

			for (id = 0; id < ts->pdata->caps->max_id; id++) {
				if ((ts->ts_curr_data.report_id_mask &
							(1 << ts->ts_curr_data.abs_data[id].id))
						&& !(ts->ts_prev_data.report_id_mask & (1 << ts->ts_curr_data.abs_data[id].id))) {
					break;
				}
			}

			if (id < MAX_FINGER) {
				memcpy(&t_ex_debug[EX_PREV_PRESS], &t_ex_debug[EX_CURR_PRESS], sizeof(struct timeval));
				do_gettimeofday(&t_ex_debug[EX_CURR_PRESS]);

				if (ts_ghost_value->check_enable.press_interval_chk) {
					if (ts->ts_prev_data.total_num
							&& is_valid_ghost_jitter(ts, ts_prev_finger_press_data, ts->ts_curr_data.abs_data[id])) {
						if (chk_time_interval(t_ex_debug[EX_CURR_PRESS], t_ex_debug[EX_PREV_PRESS], (ts_ghost_value->press_interval))) {
							ghost_detection = true;
							ghost_detection_count++;
							TOUCH_DEBUG(DEBUG_BASE_INFO, "P-R-P check .\n");
						} else	;
					} else if (!ts->ts_prev_data.total_num && ts->ts_curr_data.total_num == 1
									&& is_valid_ghost_jitter(ts, ts_prev_finger_press_data, ts->ts_curr_data.abs_data[id])) {
						if (chk_time_interval(t_ex_debug[EX_CURR_PRESS], t_ex_debug[EX_PREV_PRESS], (ts_ghost_value->press_interval))) {
							ghost_detection = true;
							TOUCH_DEBUG(DEBUG_BASE_INFO, "P-P check .\n");
						} else	;
					} else ;
				}

				if (ts_ghost_value->check_enable.diff_fingers_chk
						&& f_sub(ts->ts_prev_data.total_num, ts->ts_curr_data.total_num) >= ts_ghost_value->diff_finger_num) {
					ghost_detection = true;
					TOUCH_DEBUG(DEBUG_BASE_INFO, "check diff fingers num .\n");
				}

				memcpy(&ts_prev_finger_press_data, &ts->ts_curr_data.abs_data[id], sizeof(ts_prev_finger_press_data));
			}
		} else if (ts->ts_prev_data.total_num > ts->ts_curr_data.total_num) {
			memcpy(&t_ex_debug[EX_PREV_PRESS], &t_ex_debug[EX_CURR_PRESS], sizeof(struct timeval));
			do_gettimeofday(&t_ex_debug[EX_CURR_INT]);

			if (ts_ghost_value->check_enable.subtraction_finger_chk) {
				if (chk_time_interval(t_ex_debug[EX_CURR_INT], t_ex_debug[EX_PREV_PRESS], ts_ghost_value->subtraction_time)) {
					finger_subtraction_count++;
				} else {
					finger_subtraction_count = 0;
				}

				if (finger_subtraction_count >= ts_ghost_value->subtraction_finger_cnt) {
					finger_subtraction_count = 0;
					TOUCH_DEBUG(DEBUG_BASE_INFO, "finger subtraction check, need to rebase .\n");
					goto out_need_to_rebase;
				}
			}

		} else ;

		if (ts_ghost_value->check_enable.long_press_chk
				&& ts_ghost_value->force_continuous_mode) {
			do_gettimeofday(&t_ex_debug[EX_CURR_INT]);
			if (t_ex_debug[EX_CURR_INT].tv_sec - t_ex_debug[EX_INIT].tv_sec >= ts_ghost_value->long_press_chk_time) {
				if (ts->pdata->role->report_mode == REDUCED_REPORT_MODE) {
					TOUCH_DEBUG(DEBUG_BASE_INFO, "report mode change, continuous -> reduced:\n");
					DO_IF(touch_device_func->ic_ctrl(ts->client, IC_CTRL_REPORT_MODE, REDUCED_REPORT_MODE, &ret) != 0, error);
				}
				ts_ghost_value->force_continuous_mode = false;
			}

			long_press_check = false;
			for (cnt = 0; cnt < MAX_FINGER; cnt++) {
				if ((ts->ts_curr_data.report_id_mask & (1 << cnt))
						&& (ts->ts_prev_data.report_id_mask & (1 << cnt))
						&& is_valid_ghost_jitter(ts, ts->ts_prev_data.abs_data[cnt], ts->ts_curr_data.abs_data[cnt])) {
						long_press_check = true;
				}
			}

			if (long_press_check)
				long_press_count ++;
			else
				long_press_count = 0;

			if (long_press_count > ts_ghost_value->long_press_cnt) {
				TOUCH_DEBUG(DEBUG_BASE_INFO,
					"long_press_check_count, need to rebase .\n");
				long_press_count = 0;
				goto out_need_to_rebase;
			}
		}
	} else if (!ts->ts_curr_data.total_num) {
		long_press_count = 0;
		finger_subtraction_count = 0;
	}

	/* button ghost check (only for H/W key)*/
	if (ts_ghost_value->check_enable.button_chk
			&& ts->pdata->caps->button_support
			&& ts->ts_curr_data.state != BUTTON_CANCLED) {
		if (button_press_count == 0)
			do_gettimeofday(&t_ex_debug[EX_BUTTON_PRESS_START]);
		else
			do_gettimeofday(&t_ex_debug[EX_BUTTON_PRESS_END]);

		button_press_count++;

		if (button_press_count >= ts_ghost_value->button_int_num) {
			if (chk_time_interval(t_ex_debug[EX_BUTTON_PRESS_END], t_ex_debug[EX_BUTTON_PRESS_START], ts_ghost_value->button_duration)) {
				TOUCH_DEBUG(DEBUG_BASE_INFO, "button check, need to rebase .\n");
				goto out_need_to_rebase;
			} else;

			button_press_count = 0;
		} else {
			if ((t_ex_debug[EX_BUTTON_PRESS_END].tv_sec - t_ex_debug[EX_BUTTON_PRESS_START].tv_sec) > 1) {
				button_press_count = 0;
			}
			if (!chk_time_interval(t_ex_debug[EX_BUTTON_PRESS_END], t_ex_debug[EX_BUTTON_PRESS_START], ts_ghost_value->button_duration)) {
				button_press_count = 0;
			} else;
		}
	}

	if (ghost_detection == true && ts->ts_curr_data.total_num == 0) {
		TOUCH_DEBUG(DEBUG_BASE_INFO, "need to rebase .\n");
		goto out_need_to_rebase;
	} else if (ghost_detection == true && (ghost_detection_count >= ts_ghost_value->ghost_detection_chk_cnt)) {
		TOUCH_DEBUG(DEBUG_BASE_INFO,
			"need to rebase . ghost_detection_count: %d\n", ghost_detection_count);
		goto out_need_to_rebase;
	}

	return NO_ACTION;

out_need_to_rebase:
	return rebase_ic(ts);

out_need_to_debounce:
	return NEED_TO_OUT;

error:
	return ERROR_CASE;
}

#define HANDLE_GHOST_ALG_RET(ghost_alg_ret)\
	do {						\
		switch (ghost_alg_ret) {		\
		case NO_ACTION:				\
			break;				\
		case NEED_TO_OUT:			\
			goto do_reset_curr_data;		\
			break;				\
		case DONE_REBASE:			\
			goto ignore;		\
			break;				\
		case NEED_TO_INIT:			\
			goto do_init;			\
			break;				\
		default:				\
			goto error;			\
			break;				\
		}					\
	} while (0)

static void touch_trigger_handle(struct work_struct *work_trigger_handle)
{
	struct lge_touch_data *ts =
		container_of(to_delayed_work(work_trigger_handle),
				struct lge_touch_data,
				work_trigger_handle);
	enum ghost_error_type ghost_alg_ret = NO_ACTION;

	TOUCH_DEBUG(DEBUG_BASE_INFO, "%s\n", __func__);

	if (ta_rebase_retry_count >= MAX_RETRY_COUNT) {
		TOUCH_DEBUG(DEBUG_BASE_INFO,
				"Do not attempt to TA trigger handle .\n");
		return;
	}

	if ((ts->ts_report_data.total_num
			|| atomic_read(&ts->state.upgrade_state) == UPGRADE_START
			|| mutex_is_locked(&ts->pdata->thread_lock))
			&& ta_rebase_retry_count < MAX_RETRY_COUNT) {
		++ta_rebase_retry_count;
		TOUCH_DEBUG(DEBUG_BASE_INFO,
			"retry (%d/3) \n", ta_rebase_retry_count);
		if (touch_ta_status)
			queue_delayed_work(touch_wq,
					&ts->work_trigger_handle,
					msecs_to_jiffies(1000));
		else
			queue_delayed_work(touch_wq,
					&ts->work_trigger_handle,
					msecs_to_jiffies(0));
		return;
	} else {
		if (atomic_read(&ts->state.device_init_state) == INIT_DONE) {
			if (mutex_is_locked(&ts->pdata->thread_lock))
				return;
			mutex_lock(&ts->pdata->thread_lock);
			if (touch_ta_status)
				HANDLE_GHOST_ALG_RET(ghost_alg_ret = rebase_ic(ts));
			else
				goto do_init;
		} else
			return;
	}

do_reset_curr_data:
ignore:
	mutex_unlock(&ts->pdata->thread_lock);
	return;

do_init:
error:
	safety_reset(ts);
	touch_ic_init(ts, 0);
	mutex_unlock(&ts->pdata->thread_lock);
}


/* update_status
 *
 * Other drivers can notify their status to touch driver.
 * Do not use 'i2c_client' in other function.
 */
struct state_info       *state;
struct i2c_client       *client_only_for_update_status;
void update_status(int code, int value)
{
	if (code == NOTIFY_TA_CONNECTION) {
		if ((value == touch_ta_status) || (!boot_mode))
			return;
		else
			touch_ta_status = value;
		TOUCH_DEBUG(DEBUG_BASE_INFO, "TA Type : %d, code : %d\n", touch_ta_status, code);
		/* INVALID:0, SDP:1, DCP:2, CDP:3 PROPRIETARY:4 FLOATED:5*/

		if (!is_probe || atomic_read(&state->pm_state) > PM_RESUME)
			return;

		if (ts_data->pdata->role->ghost_detection->check_enable.ta_noise_chk) {
			ta_rebase_retry_count = 0;
			cancel_delayed_work(&ts_data->work_trigger_handle);
			if (value) {
				queue_delayed_work(touch_wq,
						&ts_data->work_trigger_handle,
						msecs_to_jiffies(1000));
			} else {
				queue_delayed_work(touch_wq,
						&ts_data->work_trigger_handle,
						msecs_to_jiffies(50));
			}
		}
		mod_delayed_work(touch_wq, &ts_data->work_sleepmode, msecs_to_jiffies(0));
	} else if (code == NOTIFY_HDMI_CONNECTION) {
		if ((value == touch_hdmi_status) || (!boot_mode))
			return;
		else
			touch_hdmi_status = value;
		TOUCH_DEBUG(DEBUG_BASE_INFO, "HDMI Connection : %d\n", touch_hdmi_status);

		if (!is_probe || atomic_read(&state->pm_state) > PM_RESUME)
			return;
	} else if (code == NOTIFY_TEMPERATURE_CHANGE)
		atomic_set(&state->temperature_state, value);
	else if (code == NOTIFY_PROXIMITY)
		atomic_set(&state->proximity_state, value ?
				PROXIMITY_NEAR : PROXIMITY_FAR);
	else if (code == NOTIFY_HALL_IC)
		atomic_set(&state->hallic_state, value ?
				HALL_COVERED : HALL_NONE);

	if (atomic_read(&state->power_state) == POWER_ON)
		touch_device_func->notify(client_only_for_update_status,
				(u8)code, value);

	TOUCH_INFO_MSG("%s : code[%d] value[%d]\n", __func__, code, value);
}
EXPORT_SYMBOL(update_status);

/* firmware_upgrade_func
 *
 */
static void firmware_upgrade_func(struct work_struct *work_upgrade)
{
	struct lge_touch_data *ts = container_of(to_delayed_work(work_upgrade),
			struct lge_touch_data, work_upgrade);
	enum error_type ret = NO_ERROR;
	long len;

	TOUCH_TRACE();

	mutex_lock(&ts->pdata->thread_lock);
	TOUCH_INFO_MSG("%s : firmware upgrade start\n", __func__);
	interrupt_control(ts, INTERRUPT_DISABLE);
	if (atomic_read(&ts->state.power_state) == POWER_OFF) {
		power_control(ts, POWER_ON);
		msleep(ts->pdata->role->booting_delay);
	}

	if(!ts->fw_info.fw_force_upgrade_cat && !ts->fw_info.fw_force_upgrade) {
		ts->pdata->role->bootloader_type = touch_device_func->get_type_bl(ts->client);
		Select_Firmware(ts, ts->pdata->role->bootloader_type);

		len = strlen(ts->pdata->inbuilt_fw_name);
		strncpy(ts->fw_info.fw_path,
			ts->pdata->inbuilt_fw_name,
			len);
	}

	atomic_set(&ts->state.upgrade_state, UPGRADE_START);
	ret = touch_device_func->fw_upgrade(ts->client,
			&ts->fw_info, ts->pdata->fw);
	atomic_set(&ts->state.upgrade_state, UPGRADE_FINISH);

	if (ret != NO_UPGRADE) {
		safety_reset(ts);
		touch_ic_init(ts, 0);
	}
	interrupt_control(ts, INTERRUPT_ENABLE);

	memset(&ts->fw_info, 0, sizeof(struct touch_firmware_module));
	ts->fw_info.fw_force_upgrade = 0;
	ts->fw_info.fw_force_upgrade_cat = 0;
	TOUCH_INFO_MSG("%s : firmware upgrade end\n", __func__);
	mutex_unlock(&ts->pdata->thread_lock);
	return;
}

static void inspection_crack_func(struct work_struct *work_crack)
{
	struct lge_touch_data *ts = container_of(to_delayed_work(work_crack),
			struct lge_touch_data, work_crack);

	mutex_lock(&ts->pdata->thread_lock);
	TOUCH_INFO_MSG("%s : crack_check start\n", __func__);
	if (atomic_read(&ts->state.upgrade_state) != UPGRADE_START) {
		atomic_set(&ts->state.crack_test_state, CRACK_TEST_START);
		window_crack_check =
			touch_device_func->inspection_crack(ts->client);
		atomic_set(&ts->state.crack_test_state, CRACK_TEST_FINISH);
	}
	TOUCH_INFO_MSG("%s : crack_check end\n", __func__);
	mutex_unlock(&ts->pdata->thread_lock);
}

static void sleep_mode_func(struct work_struct *work_sleepmode)
{
	struct lge_touch_data *ts = container_of(to_delayed_work(work_sleepmode),
			struct lge_touch_data, work_sleepmode);

	TOUCH_INFO_MSG("%s : sleep_mode change for TA start \n", __func__);
	touch_device_func->sleepmode_change(ts->client);
	TOUCH_INFO_MSG("%s : sleep_mode chagne for TA end\n", __func__);
}

/* touch_thread_irq_handler
 *
 * If you modify this function, please check the mutex.
 * Mutex should be unlocked when the thread exits this function.
 *
 * HANDLE_RET : It is used for handling the return value.
 * wake_up_device : It is used to wake up the device.
 *
 */
#define HANDLE_RET(ret)					\
	do {						\
		switch (ret) {				\
		case NO_ERROR:				\
			break;				\
		case NO_FILTER:				\
			goto do_not_filter;		\
			break;				\
		case IGNORE_EVENT_BUT_SAVE_IT:		\
			goto do_not_report;		\
			break;				\
		case IGNORE_EVENT:			\
			goto ignore;			\
			break;				\
		default: 				\
			goto error;			\
			break;				\
		}							\
	} while (0)

char *touch_wakeup_gesture[2] = { "TOUCH_GESTURE_WAKEUP=WAKEUP", NULL };

irqreturn_t touch_thread_irq_handler(int irq, void *dev_id)
{
	struct lge_touch_data *ts = (struct lge_touch_data *)dev_id;
	enum error_type ret = NO_ERROR;
	enum ghost_error_type ghost_alg_ret = NO_ACTION;
	u8 prev_reset_control = ts->pdata->pwr->reset_control;

	TOUCH_TRACE();
	mutex_lock(&ts->pdata->thread_lock);

	HANDLE_RET(ret = touch_device_func->data(ts->client,
				&ts->ts_curr_data, &ts->ts_prev_data));

	/* After filtering, report_id_mask will be changed. */
	ts->ts_curr_data.report_id_mask = ts->ts_curr_data.id_mask;

	HANDLE_RET(ret = touch_device_func->filter(ts->client,
				&ts->ts_curr_data, &ts->ts_prev_data));
	if (core_filter(ts) < 0)
		goto do_not_report;

	/* Ghost detection solution */
	if (ts->pdata->role->ghost_detection->check_enable.ghost_detection_enable) {
		HANDLE_GHOST_ALG_RET(ghost_alg_ret = ghost_detect_solution(ts));
	}

do_not_filter:
	if (ts->pdata->caps->button_support)
		DO_SAFE(key_event(ts), error);

	DO_SAFE(report_event(ts), error);
	memcpy(&ts->ts_report_data, &ts->ts_curr_data,
			sizeof(struct touch_data));

	if ((!ts->ts_report_data.total_num)
			&& (current_thermal_mode != touch_thermal_status)) {
		current_thermal_mode = touch_thermal_status;
		TOUCH_INFO_MSG("current_thermal_mode is changed: THERMAL_%s\n",
				current_thermal_mode ?
				"LOW->HIGH" : "HIGH->LOW");
		queue_delayed_work(touch_wq,
				&ts_data->work_thermal, msecs_to_jiffies(0));
	}

	if (ts->ts_report_data.total_num) {
		if (ts->pdata->swipe_pwr_ctrl_stage == WAIT_TOUCH_PRESS) {
			TOUCH_INFO_MSG("%s : Touch pressed! (swipe_pwr_ctrl_stage = %d)\n",
					__func__, ts->pdata->swipe_pwr_ctrl_stage);
			ts->pdata->swipe_pwr_ctrl_stage = WAIT_TOUCH_RELEASE;
		}
	} else {
		if (ts->pdata->swipe_pwr_ctrl_stage == WAIT_TOUCH_RELEASE) {
			TOUCH_INFO_MSG("%s : Touch released! goto do_soft_reset! (swipe_pwr_ctrl_stage = %d)\n",
					__func__, ts->pdata->swipe_pwr_ctrl_stage);
			ts->pdata->swipe_pwr_ctrl_stage = WAIT_SWIPE_WAKEUP;
			goto do_soft_reset;
		}
	}

do_not_report:
	memcpy(&ts->ts_prev_data, &ts->ts_curr_data, sizeof(struct touch_data));
do_reset_curr_data:
	memset(&ts->ts_curr_data, 0, sizeof(struct touch_data));

ignore:
	mutex_unlock(&ts->pdata->thread_lock);
	return IRQ_HANDLED;

do_init:
	safety_reset(ts);
	touch_ic_init(ts, 0);
	mutex_unlock(&ts->pdata->thread_lock);
	return IRQ_HANDLED;

do_soft_reset:
	ts->pdata->pwr->reset_control = SOFT_RESET;
	safety_reset(ts);
	ts->pdata->pwr->reset_control = prev_reset_control;
	touch_ic_init(ts, 0);
	mutex_unlock(&ts->pdata->thread_lock);
	return IRQ_HANDLED;
error:
	TOUCH_ERR_MSG("%s error\n", __func__);
	safety_reset(ts);
	touch_ic_init(ts, 0);
	mutex_unlock(&ts->pdata->thread_lock);
	TOUCH_ERR_MSG("Interrupt Handling fail\n");
	return IRQ_NONE;
}


/* touch_irq_handler
 *
 * When Interrupt occurs, it will be called before touch_thread_irq_handler.
 *
 * return
 * IRQ_HANDLED: touch_thread_irq_handler will not be called.
 * IRQ_WAKE_THREAD: touch_thread_irq_handler will be called.
 */
irqreturn_t touch_irq_handler(int irq, void *dev_id)
{
	struct lge_touch_data *ts = (struct lge_touch_data *)dev_id;

	TOUCH_TRACE();

	if (atomic_read(&ts->state.pm_state) >= PM_SUSPEND) {
		TOUCH_INFO_MSG("interrupt in suspend[%d]\n",
				atomic_read(&ts->state.pm_state));
		atomic_set(&ts->state.pm_state, PM_SUSPEND_IRQ);
		wake_lock_timeout(&ts->lpwg_wake_lock, msecs_to_jiffies(1000));
		return IRQ_HANDLED;
	}

	return IRQ_WAKE_THREAD;
}

static void change_ime_drumming_func(struct work_struct *work_ime_drumming)
{
	struct lge_touch_data *ts =
		container_of(to_delayed_work(work_ime_drumming),
				struct lge_touch_data,
				work_ime_drumming);

	printk("[Touch] %s : START !!\n", __func__);

	if (atomic_read(&ts->state.power_state) == POWER_OFF) {
		return;
	}
	mutex_lock(&ts->pdata->thread_lock);
	touch_device_func->ime_drumming(ts->client, ime_stat);
	mutex_unlock(&ts->pdata->thread_lock);

	printk("[Touch] %s : END !!\n", __func__);

	return;
}

void change_thermal_param(struct work_struct *work_thermal)
{
	struct lge_touch_data *ts = container_of(to_delayed_work(work_thermal),
			struct lge_touch_data, work_thermal);

	/*if (!pseudo_batt_info.mode) {
		TOUCH_INFO_MSG("%s: Only operates on fake battery mode\n",
				__func__);
		return;
	} else*/ if (!(ts->pdata->role->thermal_check)) {
		TOUCH_INFO_MSG("%s : thermal_check off\n", __func__);
		return;
	} else if (atomic_read(&ts->state.power_state) == POWER_OFF) {
		TOUCH_INFO_MSG("%s : touch IC power off\n", __func__);
		return;
	} else if (atomic_read(&ts->state.upgrade_state) == UPGRADE_START) {
		TOUCH_INFO_MSG("%s : touch IC upgrade\n", __func__);
		return;
	} else if (ts->ts_report_data.total_num) {
		TOUCH_INFO_MSG("%s: Finger pressed\n", __func__);
		return;
	} else {
		TOUCH_INFO_MSG("%s: Before changing thermal prarmeters...\n",
				__func__);
		mutex_lock(&ts->pdata->thread_lock);
		touch_device_func->ic_ctrl(ts->client,
				IC_CTRL_THERMAL, current_thermal_mode, NULL);
		TOUCH_INFO_MSG("%s: Thermal prarmeters are changed.\n",
				__func__);
		mutex_unlock(&ts->pdata->thread_lock);
	}

	return;
}

/* Sysfs - platform_data
 *
 * show_platform_data : Print all values of platform_data.
 * store_platform_data : User can change only the 'role'.
 */
static ssize_t show_platform_data(struct i2c_client *client, char *buf)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	struct touch_platform_data *pdata = ts->pdata;
	int ret = 0;

	ret = sprintf(buf, "====== Platform data ======\n");
	ret += sprintf(buf+ret, "int_pin[%d] reset_pin[%d]\n",
			pdata->int_pin, pdata->reset_pin);
	ret += sprintf(buf+ret, "vio_pin[%d]\n",
			pdata->vio_pin);
	ret += sprintf(buf+ret, "maker_id_pin[%d]\n",
			pdata->maker_id_pin);
	ret += sprintf(buf+ret, "caps:\n");
	ret += sprintf(buf+ret, "\t%25s = %d\n", "button_support",
			pdata->caps->button_support);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "number_of_button",
			pdata->caps->number_of_button);
	ret += sprintf(buf+ret, "\t%25s = %d, %d, %d, %d\n", "button_name",
			pdata->caps->button_name[0],
			pdata->caps->button_name[1],
			pdata->caps->button_name[2],
			pdata->caps->button_name[3]);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "max_x", pdata->caps->max_x);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "max_y", pdata->caps->max_y);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "max_pressure",
			pdata->caps->max_pressure);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "max_width",
			pdata->caps->max_width);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "max_orientation",
			pdata->caps->max_width);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "max_id",
			pdata->caps->max_id);
	ret += sprintf(buf+ret, "role:\n");
	ret += sprintf(buf+ret, "\t%25s = %d\n", "protocol_type",
			pdata->role->protocol_type);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "report_mode",
			pdata->role->report_mode);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "delta_pos_threshold",
			pdata->role->delta_pos_threshold);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "booting_delay",
			pdata->role->booting_delay);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "reset_delay",
			pdata->role->reset_delay);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "wake_up_by_touch",
			pdata->role->wake_up_by_touch);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "use_sleep_mode",
			pdata->role->use_sleep_mode);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "use_lpwg_all",
			pdata->role->use_lpwg_all);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "use_security_mode",
			pdata->role->use_security_mode);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "use_lcd_notifier_callback",
			pdata->role->use_lcd_notifier_callback);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "thermal_check",
			pdata->role->thermal_check);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "palm_ctrl_mode",
			pdata->role->palm_ctrl_mode);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "use_hover_finger",
			pdata->role->use_hover_finger);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "use_rmi_dev",
			pdata->role->use_rmi_dev);
	ret += sprintf(buf+ret, "\t%25s = 0x%lx\n", "irqflags",
			pdata->role->irqflags);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "bouncing.enable",
			pdata->role->bouncing_filter->enable);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "grip.enable",
			pdata->role->grip_filter->enable);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "grip.edge_region",
			pdata->role->grip_filter->edge_region);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "grip.max_delta",
			pdata->role->grip_filter->max_delta);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "grip.width_ratio",
			pdata->role->grip_filter->width_ratio);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "accuracy.enable",
			pdata->role->accuracy_filter->enable);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "accuracy.min_delta",
			pdata->role->accuracy_filter->min_delta);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "accuracy.curr_ratio",
			pdata->role->accuracy_filter->curr_ratio);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "accuracy.min_pressure",
			pdata->role->accuracy_filter->min_pressure);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "jitter.enable",
			pdata->role->jitter_filter->enable);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "jitter.curr_ratio",
			pdata->role->jitter_filter->curr_ratio);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "quickcover.enable",
			pdata->role->quickcover_filter->enable);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "quickcover.X1",
			pdata->role->quickcover_filter->X1);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "quickcover.Y1",
			pdata->role->quickcover_filter->Y1);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "quickcover.X2",
			pdata->role->quickcover_filter->X2);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "quickcover.Y2",
			pdata->role->quickcover_filter->Y2);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "ghost_detection_enable",
			pdata->role->ghost_detection->check_enable.ghost_detection_enable);
	ret += sprintf(buf+ret, "pwr:\n");
	ret += sprintf(buf+ret, "\t%25s = %d\n", "use_regulator",
			pdata->pwr->use_regulator);
	ret += sprintf(buf+ret, "\t%25s = %s\n", "vdd", pdata->pwr->vdd);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "vdd_voltage",
			pdata->pwr->vdd_voltage);
	ret += sprintf(buf+ret, "\t%25s = %s\n", "vio", pdata->pwr->vio);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "vio_voltage",
			pdata->pwr->vio_voltage);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "vio_control",
			pdata->pwr->vio_control);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "reset_control",
			pdata->pwr->reset_control);
	ret += sprintf(buf+ret, "\t%25s = %s\n", "power",
			pdata->pwr->power ? "YES" : "NO");
	ret += sprintf(buf+ret, "firmware:\n");
	ret += sprintf(buf+ret, "\t%25s = %s\n", "fw_image",
		pdata->inbuilt_fw_name);
	ret += sprintf(buf+ret, "\t%25s = %d\n", "need_upgrade",
			pdata->fw->need_upgrade);

	return ret;
}

static int add_filter_func(struct lge_touch_data *ts);
static void remove_filter_func(struct lge_touch_data *ts);
static ssize_t store_platform_data(struct i2c_client *client,
		const char *buf, size_t count)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);

	unsigned char string[30] = {0, };
	u32 value = 0;

	/* It only can change the 'role'.
	 * (because changing 'others' is so dangerous)
	 * Therefore, if you want to change some variables by using 'sysfs',
	 * insert them in 'role' structure.
	 * After store, device should be re-initialized.
	 */

	sscanf(buf, "%s %d", string, &value);

	if (!strcmp(string, "protocol_type"))
		ts->pdata->role->protocol_type = value;
	else if (!strcmp(string, "report_mode"))
		ts->pdata->role->report_mode = value;
	else if (!strcmp(string, "delta_pos_threshold"))
		ts->pdata->role->delta_pos_threshold = value;
	else if (!strcmp(string, "booting_delay"))
		ts->pdata->role->booting_delay = value;
	else if (!strcmp(string, "reset_delay"))
		ts->pdata->role->reset_delay = value;
	else if (!strcmp(string, "wake_up_by_touch"))
		ts->pdata->role->wake_up_by_touch = value;
	else if (!strcmp(string, "use_sleep_mode"))
		ts->pdata->role->use_sleep_mode = value;
	else if (!strcmp(string, "use_lpwg_all"))
		ts->pdata->role->use_lpwg_all = value;
	else if (!strcmp(string, "use_security_mode"))
		ts->pdata->role->use_security_mode = value;
	else if (!strcmp(string, "use_lcd_notifier_callback"))
		ts->pdata->role->use_lcd_notifier_callback = value;
	else if (!strcmp(string, "thermal_check"))
		ts->pdata->role->thermal_check = value;
	else if (!strcmp(string, "palm_ctrl_mode"))
		ts->pdata->role->palm_ctrl_mode = value;
	else if (!strcmp(string, "use_hover_finger"))
		ts->pdata->role->use_hover_finger = value;
	else if (!strcmp(string, "use_rmi_dev"))
		ts->pdata->role->use_rmi_dev = value;
	else if (!strcmp(string, "irqflags"))
		ts->pdata->role->irqflags = value;
	else if (!strcmp(string, "bouncing.enable"))
		ts->pdata->role->bouncing_filter->enable = value;
	else if (!strcmp(string, "grip.enable"))
		ts->pdata->role->grip_filter->enable = value;
	else if (!strcmp(string, "grip.edge_region"))
		ts->pdata->role->grip_filter->edge_region = value;
	else if (!strcmp(string, "grip.max_delta"))
		ts->pdata->role->grip_filter->max_delta = value;
	else if (!strcmp(string, "grip.width_ratio"))
		ts->pdata->role->grip_filter->width_ratio = value;
	else if (!strcmp(string, "quickcover.enable"))
		ts->pdata->role->quickcover_filter->enable = value;
	else if (!strcmp(string, "quickcover.X1"))
		ts->pdata->role->quickcover_filter->X1 = value;
	else if (!strcmp(string, "quickcover.X2"))
		ts->pdata->role->quickcover_filter->X2 = value;
	else if (!strcmp(string, "quickcover.Y1"))
		ts->pdata->role->quickcover_filter->Y1 = value;
	else if (!strcmp(string, "quickcover.Y2"))
		ts->pdata->role->quickcover_filter->Y2 = value;
	else if (!strcmp(string, "accuracy.enable"))
		ts->pdata->role->accuracy_filter->enable = value;
	else if (!strcmp(string, "accuracy.min_delta"))
		ts->pdata->role->accuracy_filter->min_delta = value;
	else if (!strcmp(string, "accuracy.curr_ratio"))
		ts->pdata->role->accuracy_filter->curr_ratio = value;
	else if (!strcmp(string, "accuracy.min_pressure"))
		ts->pdata->role->accuracy_filter->min_pressure = value;
	else if (!strcmp(string, "jitter.enable"))
		ts->pdata->role->jitter_filter->enable = value;
	else if (!strcmp(string, "jitter.curr_ratio"))
		ts->pdata->role->jitter_filter->curr_ratio = value;

	remove_filter_func(ts);
	add_filter_func(ts);

	return count;
}


/* Sysfs - power_ctrl
 *
 * store_power_ctrl : User can control the power - on, off, reset, init.
 */
static ssize_t store_power_ctrl(struct i2c_client *client,
		const char *buf, size_t count)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);

	unsigned char string[30] = {0, };
	u32 value = 0;

	sscanf(buf, "%s %d", string, &value);

	mutex_lock(&ts->pdata->thread_lock);
	if (!strcmp(string, "reset")) {
		safety_reset(ts);
		touch_ic_init(ts, 0);
	} else if (!strcmp(string, "init")) {
		touch_ic_init(ts, 0);
	} else if (!strcmp(string, "power")) {
		if (value)
			touch_device_func->power(ts->client, POWER_ON);
		else
			touch_device_func->power(ts->client, POWER_OFF);
	} else if (!strcmp(string, "safe_power")) {
		if (value) {
			power_control(ts, POWER_ON);
			msleep(ts->pdata->role->booting_delay);
		} else
			power_control(ts, POWER_OFF);
	}

	mutex_unlock(&ts->pdata->thread_lock);
	return count;
}


/* Sysfs -ic_rw
 *
 * show_ic_rw : User can read the register using 'reg' and 'value'.
 *                     Both 'reg' and 'value' are assigned by 'store_ic_rw'.
 *                     Use 'assign' command.
 * store_ic_rw : User can write values to registers.
 *
 * reg, value : these variables are used to read the register.
 */
static int reg = 0, value = 0;

static ssize_t show_ic_rw(struct i2c_client *client, char *buf)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	u32 ret = 0, tmp = 0;

	if (atomic_read(&ts->state.power_state) != POWER_ON)
		return ret;
		mutex_lock(&ts->pdata->thread_lock);

	do {
		touch_device_func->ic_ctrl(ts->client,
				IC_CTRL_READ, reg++, &tmp);
		ret += sprintf(buf+ret, "%d\n", tmp);
	} while (--value > 0);
		mutex_unlock(&ts->pdata->thread_lock);

	TOUCH_DEBUG(DEBUG_BASE_INFO, "%s\n", buf);
	return ret;
}

static ssize_t store_ic_rw(struct i2c_client *client,
		const char *buf, size_t count)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);

	unsigned char string[30] = {0, };
	int temp[2] = {0, };
	u32 ret = 0;

	sscanf(buf, "%s %d %d", string, &temp[0], &temp[1]);

	if (atomic_read(&ts->state.power_state) != POWER_ON ||
			(strcmp(string, "write") && strcmp(string, "assign")))
		return count;

	reg = temp[0];
	value = temp[1];
	if (!strcmp(string, "write")) {
		u32 write_data = ((0xFF & reg) << 16) | (0xFF & value);
		mutex_lock(&ts->pdata->thread_lock);
		touch_device_func->ic_ctrl(ts->client,
				IC_CTRL_WRITE, write_data, &ret);
		mutex_unlock(&ts->pdata->thread_lock);
	}

	TOUCH_DEBUG(DEBUG_BASE_INFO,
			"%s - reg[%d] value[%d] return[%d]\n",
			string, reg, value, ret);
	return count;
}


/* Sysfs - notify
 *
 * shoe_notify : Print states of both TA and Temperature.
 * store_notify : Notify changes of device environment such as TA-connection
 * or temperature to specific_driver.
 */
static ssize_t show_notify(struct i2c_client *client, char *buf)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	int ret = 0;

	ret = sprintf(buf, "TA[%d] Temperature[%d] Proximity[%d]\n",
			atomic_read(&ts->state.ta_state),
			atomic_read(&ts->state.temperature_state),
			atomic_read(&ts->state.proximity_state));
	return ret;
}

static ssize_t store_notify(struct i2c_client *client,
		const char *buf, size_t count)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	int code, value;

	sscanf(buf, "%d %d", &code, &value);

	if (code == NOTIFY_TA_CONNECTION)
		atomic_set(&ts->state.ta_state, value ?
				TA_CONNECTED : TA_DISCONNECTED);
	else if (code == NOTIFY_TEMPERATURE_CHANGE)
		atomic_set(&ts->state.temperature_state, value);
	else if (code == NOTIFY_PROXIMITY)
		atomic_set(&ts->state.proximity_state, value ?
				PROXIMITY_NEAR : PROXIMITY_FAR);

	if (atomic_read(&ts->state.power_state) == POWER_ON)
		touch_device_func->notify(ts->client, (u8)code, value);

	TOUCH_DEBUG(DEBUG_BASE_INFO, "code[%d] value[%d]\n", code, value);
	return count;
}


/* Sysfs - firmware_upgrade
 *
 * store_upgrade : upgrade the firmware.
 */
static ssize_t store_upgrade(struct i2c_client *client,
		const char *buf, size_t count)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	char path[256] = {0, };

	sscanf(buf, "%s", path);

	memcpy(ts->fw_info.fw_path, path, sizeof(ts->fw_info.fw_path));
	ts->fw_info.fw_force_upgrade = 1;

	queue_delayed_work(touch_wq, &ts->work_upgrade, 0);

	return count;
}

/* show_upgrade to upgrade via adb command 'cat'
 * in case permission problem happend */
static ssize_t show_upgrade(struct i2c_client *client, char *buf)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	int ret = 0;
	int len = 0;

	len = strlen(ts->pdata->inbuilt_fw_name);
	strncpy(ts->fw_info.fw_path,
		ts->pdata->inbuilt_fw_name, len);
	ts->fw_info.fw_force_upgrade_cat = 1;

	queue_delayed_work(touch_wq, &ts->work_upgrade, 0);

	return ret;
}
/* Sysfs - lpwg_data (Low Power Wake-up Gesture)
 *
 * read : "x1 y1\n x2 y2\n ..."
 * write
 * 1 : ENABLE/DISABLE
 * 2 : LCD SIZE
 * 3 : ACTIVE AREA
 * 4 : TAP COUNT
 */
static struct point lpwg_data[MAX_POINT_SIZE_FOR_LPWG];
static ssize_t show_lpwg_data(struct i2c_client *client, char *buf)
{
	int i = 0, ret = 0;

	if (touch_device_func->lpwg) {
		memset(lpwg_data, 0,
				sizeof(struct point)*MAX_POINT_SIZE_FOR_LPWG);
		touch_device_func->lpwg(client, LPWG_READ, 0, lpwg_data);
		for (i = 0; i < MAX_POINT_SIZE_FOR_LPWG; i++) {
			if (lpwg_data[i].x == -1 && lpwg_data[i].y == -1)
				break;
			ret += sprintf(buf+ret, "%d %d\n",
					lpwg_data[i].x, lpwg_data[i].y);
		}
	}
	return ret;
}

static ssize_t store_lpwg_data(struct i2c_client *client,
		const char *buf, size_t count)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	int reply = 0;

	sscanf(buf, "%d", &reply);

	TOUCH_INFO_MSG("%s : reply = %d\n", __func__, reply);

	if (reply == 0)
		ts->pdata->swipe_pwr_ctrl_stage = WAIT_SWIPE_WAKEUP;

	if (touch_device_func->lpwg) {
		mutex_lock(&ts->pdata->thread_lock);
		touch_device_func->lpwg(client, LPWG_REPLY, reply, NULL);
		mutex_unlock(&ts->pdata->thread_lock);
	}

	atomic_set(&ts->state.uevent_state, UEVENT_IDLE);
	wake_unlock(&ts->lpwg_wake_lock);

	return count;
}

/* Sysfs - lpwg_notify (Low Power Wake-up Gesture)
 *
 * write
 * 1 : ENABLE/DISABLE
 * 2 : LCD SIZE
 * 3 : ACTIVE AREA
 * 4 : TAP COUNT
 * 5 : TAP DISTANCE
 * 6 : LCD ON/OFF
 * 7 : SENSOR STATUS
 * 8 : Double Tap Check
 */
static ssize_t store_lpwg_notify(struct i2c_client *client,
		const char *buf, size_t count)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	int type = 0;
	int value[4] = {0, };

	sscanf(buf, "%d %d %d %d %d",
			&type, &value[0], &value[1], &value[2], &value[3]);

	if (ts->pdata->role->use_lpwg_all) {
		if (type == 6 || type == 7)
			return count;
	} else {
		if (type == 9)
			return count;
	}

	TOUCH_DEBUG(DEBUG_BASE_INFO, "LPWG: type[%d] value[%d/%d/%d/%d]\n",
			type, value[0], value[1], value[2], value[3]);

	if (touch_device_func->lpwg) {
		mutex_lock(&ts->pdata->thread_lock);
		switch (type) {
		case 1:
			touch_device_func->lpwg(client,
					LPWG_ENABLE, value[0], NULL);

			knock_mode = (ts->pdata->role->use_security_mode)? value[0] : 0;
			break;
		case 2:
			touch_device_func->lpwg(client,
					LPWG_LCD_X, value[0], NULL);
			touch_device_func->lpwg(client,
					LPWG_LCD_Y, value[1], NULL);
			break;
		case 3:
			touch_device_func->lpwg(client,
					LPWG_ACTIVE_AREA_X1,
					value[0], NULL);
			touch_device_func->lpwg(client,
					LPWG_ACTIVE_AREA_X2,
					value[1], NULL);
			touch_device_func->lpwg(client,
					LPWG_ACTIVE_AREA_Y1,
					value[2], NULL);
			touch_device_func->lpwg(client,
					LPWG_ACTIVE_AREA_Y2,
					value[3], NULL);
			break;
		case 4:
			touch_device_func->lpwg(client,
					LPWG_TAP_COUNT, value[0], NULL);
			break;
		case 5:
			touch_device_func->lpwg(client,
					LPWG_LENGTH_BETWEEN_TAP,
					value[0], NULL);
			break;
		case 6:
			touch_device_func->lpwg(client,
					LPWG_EARLY_SUSPEND, value[0], NULL);
			break;
		case 7:
			touch_device_func->lpwg(client,
					LPWG_SENSOR_STATUS, value[0], NULL);
			break;
		case 8:
			touch_device_func->lpwg(client,
					LPWG_DOUBLE_TAP_CHECK, value[0], NULL);
			break;
		case 9:
			touch_device_func->lpwg(client,
					LPWG_UPDATE_ALL, (int64_t)&value[0], NULL);
			break;
		default:
			break;
		}
		mutex_unlock(&ts->pdata->thread_lock);
	}
	return count;
}
/* store_keyguard_info
 *
 * This function is related with Keyguard in framework.
 */
static ssize_t store_keyguard_info(struct i2c_client *client,
		const char *buf, size_t count)
{
	int value;
	struct lge_touch_data *ts = i2c_get_clientdata(client);

	sscanf(buf, "%d", &value);

	switch (value) {
	case KEYGUARD_RESERVED:
		lockscreen_stat = 0;
		printk("[Touch] %s : Lockscreen unlocked\n", __func__);
		if (ime_stat) {
			printk("[Touch] %s : IME ON , Lockscreen unlocked\n",
					__func__);
			queue_delayed_work(touch_wq, &ts->work_ime_drumming,
					msecs_to_jiffies(10));
		}
		break;
	case KEYGUARD_ENABLE:
		lockscreen_stat = 1;
		printk("[Touch] %s : Lockscreen locked\n", __func__);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t show_ime_drumming_status(struct i2c_client *client, char *buf)
{
	int ret = 0;
	/* TO DO : */

	return ret;
}

static ssize_t store_ime_drumming_status(struct i2c_client *client,
		const char *buf, size_t count)
{
	int value;
	struct lge_touch_data *ts = i2c_get_clientdata(client);

	sscanf(buf, "%d", &value);

	switch (value) {
	case IME_OFF:
		if (ime_stat) {
			ime_stat = 0;
			printk("[Touch] %s : IME OFF\n", __func__);
			queue_delayed_work(touch_wq, &ts->work_ime_drumming,
					msecs_to_jiffies(10));
		}
		break;
	case IME_ON:
		if (!ime_stat) {
			ime_stat = 1;
			printk("[Touch] %s : IME ON\n", __func__);
			queue_delayed_work(touch_wq, &ts->work_ime_drumming,
					msecs_to_jiffies(10));
		}
		break;
	case IME_SWYPE:
		if (ime_stat != 2) {
			ime_stat = 2;
			printk("[Touch] %s : IME ON and swype ON\n", __func__);
			queue_delayed_work(touch_wq, &ts->work_ime_drumming,
					msecs_to_jiffies(10));
		}
		break;
	default:
		break;
	}

	return count;
}

static ssize_t store_quick_cover_status(struct i2c_client *client,
		const char *buf, size_t count)
{
	int value;
	sscanf(buf, "%d", &value);

	if ((value == QUICKCOVER_CLOSE) &&
			(quick_cover_status == QUICKCOVER_OPEN)) {
		quick_cover_status = QUICKCOVER_CLOSE;
	} else if ((value == QUICKCOVER_OPEN) &&
			(quick_cover_status == QUICKCOVER_CLOSE)) {
		quick_cover_status = QUICKCOVER_OPEN;
	} else {
		return count;
	}

	TOUCH_INFO_MSG("quick cover status = %s\n",
			(quick_cover_status == QUICKCOVER_CLOSE) ?
			"QUICK_COVER_CLOSE" : "QUICK_COVER_OPEN");
	return count;
}
static ssize_t store_incoming_call(struct i2c_client *client,
		const char *buf, size_t count)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	static char incoming_call_str[3][8] = {"IDLE", "RINGING", "OFFHOOK"};
	int value;

	sscanf(buf, "%d", &value);

	atomic_set(&ts->state.incoming_call_state, value);

	if (value <= INCOMING_CALL_OFFHOOK) {
		TOUCH_INFO_MSG("%s : %s(%d) \n", __func__, incoming_call_str[atomic_read(&ts->state.incoming_call_state)], atomic_read(&ts->state.incoming_call_state));
	} else {
		TOUCH_INFO_MSG("%s : %d \n", __func__, atomic_read(&ts->state.incoming_call_state));
	}
	return count;
}

static ssize_t show_window_crack_status(struct i2c_client *client, char *buf)
{
	int ret = 0;
	ret = sprintf(buf, "window crack status [%d]\n", window_crack_check);
	return ret;
}

static ssize_t store_window_crack_status(struct i2c_client *client,
		const char *buf, size_t count)
{
	int value;
	sscanf(buf, "%d", &value);

	if (value == 1)
		window_crack_check = CRACK;
	else
		window_crack_check = NO_CRACK;

	return count;
}

static ssize_t show_lpwg_all(struct i2c_client *client, char *buf)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	int ret = 0;

	ret = sprintf(buf, "%s : LPWG all is %s\n", __func__,
			ts->pdata->role->use_lpwg_all ? "enabled" : "disabled");

	return ret;
}

static ssize_t store_lpwg_all(struct i2c_client *client,
		const char *buf, size_t count)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);
	int value;

	sscanf(buf, "%d", &value);

	ts->pdata->role->use_lpwg_all = value;
	TOUCH_INFO_MSG("%s : %s LPWG all.\n", __func__,
			ts->pdata->role->use_lpwg_all ? "Enable" : "Disable");

	return count;
}

static LGE_TOUCH_ATTR(platform_data,
		S_IRUGO | S_IWUSR, show_platform_data, store_platform_data);
static LGE_TOUCH_ATTR(power_ctrl, S_IRUGO | S_IWUSR, NULL, store_power_ctrl);
static LGE_TOUCH_ATTR(ic_rw, S_IRUGO | S_IWUSR, show_ic_rw, store_ic_rw);
static LGE_TOUCH_ATTR(notify, S_IRUGO | S_IWUSR, show_notify, store_notify);
static LGE_TOUCH_ATTR(fw_upgrade, S_IRUGO | S_IWUSR,
		show_upgrade, store_upgrade);
static LGE_TOUCH_ATTR(lpwg_data,
		S_IRUGO | S_IWUSR, show_lpwg_data, store_lpwg_data);
static LGE_TOUCH_ATTR(lpwg_notify, S_IRUGO | S_IWUSR, NULL, store_lpwg_notify);
static LGE_TOUCH_ATTR(keyguard, S_IRUGO | S_IWUSR, NULL, store_keyguard_info);
static LGE_TOUCH_ATTR(ime_status, S_IRUGO | S_IWUSR,
		show_ime_drumming_status, store_ime_drumming_status);
static LGE_TOUCH_ATTR(quick_cover_status, S_IRUGO | S_IWUSR,
		NULL, store_quick_cover_status);
static LGE_TOUCH_ATTR(incoming_call, S_IRUGO | S_IWUSR,
		NULL, store_incoming_call);
static LGE_TOUCH_ATTR(crack_status, S_IRUGO | S_IWUSR,
		show_window_crack_status, store_window_crack_status);
static LGE_TOUCH_ATTR(lpwg_all, S_IRUGO | S_IWUSR,
		show_lpwg_all, store_lpwg_all);

static struct attribute *lge_touch_attribute_list[] = {
	&lge_touch_attr_platform_data.attr,
	&lge_touch_attr_power_ctrl.attr,
	&lge_touch_attr_ic_rw.attr,
	&lge_touch_attr_notify.attr,
	&lge_touch_attr_fw_upgrade.attr,
	&lge_touch_attr_lpwg_data.attr,
	&lge_touch_attr_lpwg_notify.attr,
	&lge_touch_attr_keyguard.attr,
	&lge_touch_attr_ime_status.attr,
	&lge_touch_attr_quick_cover_status.attr,
	&lge_touch_attr_incoming_call.attr,
	&lge_touch_attr_crack_status.attr,
	&lge_touch_attr_lpwg_all.attr,
	NULL,
};

static const struct attribute_group lge_touch_attribute_group = {
	.attrs = lge_touch_attribute_list,
};

static ssize_t lge_touch_attr_show(struct kobject *lge_touch_kobj,
		struct attribute *attr, char *buf)
{
	struct lge_touch_data *ts = container_of(lge_touch_kobj,
			struct lge_touch_data, lge_touch_kobj);
	struct lge_touch_attribute *lge_touch_priv =
		container_of(attr, struct lge_touch_attribute, attr);
	ssize_t ret = 0;

	if (lge_touch_priv->show)
		ret = lge_touch_priv->show(ts->client, buf);

	return ret;
}

static ssize_t lge_touch_attr_store(struct kobject *lge_touch_kobj,
		struct attribute *attr,
		const char *buf, size_t count)
{
	struct lge_touch_data *ts = container_of(lge_touch_kobj,
			struct lge_touch_data, lge_touch_kobj);
	struct lge_touch_attribute *lge_touch_priv =
		container_of(attr, struct lge_touch_attribute, attr);
	ssize_t ret = 0;

	if (lge_touch_priv->store)
		ret = lge_touch_priv->store(ts->client, buf, count);

	return ret;
}

static const struct sysfs_ops lge_touch_sysfs_ops = {
	.show	= lge_touch_attr_show,
	.store	= lge_touch_attr_store,
};

static struct kobj_type lge_touch_kobj_type = {
	.sysfs_ops	= &lge_touch_sysfs_ops,
};


/* get_dts_data
 *
 * make platform data
 */
#define GET_PROPERTY_U8(np, string, target)			\
	do {								\
		u32 tmp_val = 0;					\
		if (of_property_read_u32(np, string, &tmp_val) < 0) 	\
		target = 0;					\
		else							\
		target = (u8) tmp_val;		\
	} while (0)
#define GET_PROPERTY_U32(np, string, target)			\
	do {								\
		u32 tmp_val = 0;					\
		if (of_property_read_u32(np, string, &tmp_val) < 0)	\
		target = -1;					\
		else							\
		target = tmp_val;					\
	} while (0)

#define GET_PROPERTY_U32_ARRAY(np, string, target, size)		\
	do {								\
		struct property *prop = of_find_property(np, string, NULL); \
		if (prop && prop->value && prop->length == size) {	\
			int i = 0;					\
			const u8 *iprop = prop->value;			\
			for (i = 0; i < prop->length; i++)		\
			target[i] = (u32)iprop[i];			\
		}							\
	} while (0)

#define GET_PROPERTY_U32_ARRAY_2(np, string, target, size, save)		\
	do {								\
		struct property *prop = of_find_property(np, string, NULL); \
		u32 temp_val = 0;					\
		u32 temp_array[size] = {0};				\
		if (prop && prop->value) {	\
			int i = 0;					\
			temp_val = prop->length / sizeof(u32);			\
			if (save != NULL)						\
				*((u32*)save) = temp_val;							\
			if (temp_val <= size) {	\
				of_property_read_u32_array(np, string, temp_array, temp_val);		\
				for (i = 0; i < temp_val; i++)		\
				target[i] = temp_array[i];			\
			} else {						\
			}							\
		}							\
	} while (0)

#define GET_PROPERTY_STRING(np, string, target)				\
	do {								\
		const char *tmp_val = np->name;				\
		if (of_property_read_string(np, string, &tmp_val) < 0)	\
		strncpy(target, " ", 1);				\
		else {							\
			int len = strlen(tmp_val);			\
			memcpy(target, tmp_val, len);			\
		}							\
	} while (0)

static struct touch_platform_data *get_dts_data(struct device *dev)
{
	struct touch_platform_data *p_data;
	struct device_node *np;
	int rc, i = 0;

	ASSIGN(np = dev->of_node, error_mem);
	ASSIGN(p_data = devm_kzalloc(dev,
				sizeof(struct touch_platform_data),
				GFP_KERNEL), error_mem);
	ASSIGN(p_data->caps = devm_kzalloc(dev,
				sizeof(struct touch_device_caps),
				GFP_KERNEL), error_mem);
	ASSIGN(p_data->role = devm_kzalloc(dev,
				sizeof(struct touch_operation_role),
				GFP_KERNEL), error_mem);
	ASSIGN(p_data->pwr = devm_kzalloc(dev,
				sizeof(struct touch_power_module),
				GFP_KERNEL), error_mem);
	ASSIGN(p_data->fw = devm_kzalloc(dev,
				sizeof(struct touch_firmware_module),
				GFP_KERNEL), error_mem);

	ASSIGN(p_data->role->bouncing_filter = devm_kzalloc(dev,
				sizeof(struct bouncing_filter_role),
				GFP_KERNEL), error_mem);
	ASSIGN(p_data->role->grip_filter = devm_kzalloc(dev,
				sizeof(struct grip_filter_role),
				GFP_KERNEL), error_mem);
	ASSIGN(p_data->role->accuracy_filter = devm_kzalloc(dev,
				sizeof(struct accuracy_filter_role),
				GFP_KERNEL), error_mem);
	ASSIGN(p_data->role->jitter_filter = devm_kzalloc(dev,
				sizeof(struct jitter_filter_role),
				GFP_KERNEL), error_mem);
	ASSIGN(p_data->role->quickcover_filter = devm_kzalloc(dev,
				sizeof(struct quickcover_filter_role),
				GFP_KERNEL), error_mem);
	ASSIGN(p_data->role->ghost_detection = devm_kzalloc(dev,
				sizeof(struct ghost_detection_role),
				GFP_KERNEL), error_mem);
	ASSIGN(p_data->role->crack_detection = devm_kzalloc(dev,
				sizeof(struct crack_detection_role),
				GFP_KERNEL), error_mem);

	//firmware
	rc = of_property_count_strings(np, "fw_image");
	TOUCH_INFO_MSG("firmware path count = %d ", rc);
	for (i = 0; i < rc; i++) {
		of_property_read_string_index(np, "fw_image", i, &p_data->inbuilt_fw_name_list[i]);
		TOUCH_INFO_MSG("fw_image [%d]: %s\n", i, p_data->inbuilt_fw_name_list[i]);
	}

	GET_PROPERTY_U32_ARRAY_2(np, "fw_pid_addr", p_data->fw_pid_addr_list, 4,
			NULL);
	GET_PROPERTY_U32_ARRAY_2(np, "fw_ver_addr", p_data->fw_ver_addr_list, 4,
			NULL);

	// PANEL
	p_data->panel_spec = NULL;
	p_data->panel_spec_mfts = NULL;
	of_property_read_string(np, "panel_spec", &p_data->panel_spec);
	of_property_read_string(np, "panel_spec_mfts", &p_data->panel_spec_mfts);
	GET_PROPERTY_U32_ARRAY_2(np, "ref_chk_option", p_data->ref_chk_option, 4,
			NULL);
	GET_PROPERTY_U32_ARRAY_2(np, "tx_cap", p_data->tx_cap, TRX_MAX,
		&p_data->tx_ch_count);
	GET_PROPERTY_U32_ARRAY_2(np, "rx_cap", p_data->rx_cap, TRX_MAX,
		&p_data->rx_ch_count);
	if (p_data->tx_ch_count && p_data->rx_ch_count) {
		p_data->tx_ch_count ++;
		p_data->rx_ch_count ++;
	}

	// GPIO
	p_data->reset_pin = of_get_named_gpio_flags(np, "reset-gpio", 0, NULL);
	p_data->int_pin = of_get_named_gpio_flags(np, "irq-gpio", 0, NULL);
	p_data->vio_pin = of_get_named_gpio_flags(np, "vio-gpio", 0, NULL);
	p_data->maker_id_pin = of_get_named_gpio_flags(np, "maker_id-gpio", 0, NULL);

	// CAPS
	GET_PROPERTY_U32(np, "button_support", p_data->caps->button_support);
	GET_PROPERTY_U32(np, "number_of_button",
			p_data->caps->number_of_button);
	GET_PROPERTY_U32_ARRAY(np, "button_name", p_data->caps->button_name,
			p_data->caps->number_of_button);
	GET_PROPERTY_U32(np, "max_x", p_data->caps->max_x);
	GET_PROPERTY_U32(np, "max_y", p_data->caps->max_y);
	GET_PROPERTY_U32(np, "max_pressure", p_data->caps->max_pressure);
	GET_PROPERTY_U32(np, "max_width", p_data->caps->max_width);
	GET_PROPERTY_U32(np, "max_orientation", p_data->caps->max_orientation);
	GET_PROPERTY_U32(np, "max_id", p_data->caps->max_id);

	// ROLE
	GET_PROPERTY_U32(np, "protocol_type", p_data->role->protocol_type);
	GET_PROPERTY_U32(np, "report_mode", p_data->role->report_mode);
	GET_PROPERTY_U32(np, "delta_pos_threshold",
			p_data->role->delta_pos_threshold);
	GET_PROPERTY_U32(np, "booting_delay", p_data->role->booting_delay);
	GET_PROPERTY_U32(np, "reset_delay", p_data->role->reset_delay);
	GET_PROPERTY_U32(np, "wake_up_by_touch",
			p_data->role->wake_up_by_touch);
	GET_PROPERTY_U32(np, "use_sleep_mode", p_data->role->use_sleep_mode);
	GET_PROPERTY_U32(np, "use_lpwg_all", p_data->role->use_lpwg_all);
	GET_PROPERTY_U32(np, "use_security_mode",
			p_data->role->use_security_mode);
	GET_PROPERTY_U32(np, "use_lcd_notifier_callback", p_data->role->use_lcd_notifier_callback);
	GET_PROPERTY_U32(np, "thermal_check", p_data->role->thermal_check);
	GET_PROPERTY_U32(np, "palm_ctrl_mode", p_data->role->palm_ctrl_mode);
	GET_PROPERTY_U32(np, "use_hover_finger", p_data->role->use_hover_finger);
	GET_PROPERTY_U32(np, "use_rmi_dev", p_data->role->use_rmi_dev);
	GET_PROPERTY_U32(np, "irqflags", p_data->role->irqflags);
	GET_PROPERTY_U32(np, "bouncing.enable",
			p_data->role->bouncing_filter->enable);
	GET_PROPERTY_U32(np, "grip.enable", p_data->role->grip_filter->enable);
	GET_PROPERTY_U32(np, "grip.edge_region",
			p_data->role->grip_filter->edge_region);
	GET_PROPERTY_U32(np, "grip.max_delta",
			p_data->role->grip_filter->max_delta);
	GET_PROPERTY_U32(np, "grip.width_ratio",
			p_data->role->grip_filter->width_ratio);
	GET_PROPERTY_U32(np, "accuracy.enable",
			p_data->role->accuracy_filter->enable);
	GET_PROPERTY_U32(np, "accuracy.min_delta",
			p_data->role->accuracy_filter->min_delta);
	GET_PROPERTY_U32(np, "accuracy.curr_ratio",
			p_data->role->accuracy_filter->curr_ratio);
	GET_PROPERTY_U32(np, "accuracy.min_pressure",
			p_data->role->accuracy_filter->min_pressure);
	GET_PROPERTY_U32(np, "jitter.enable",
			p_data->role->jitter_filter->enable);
	GET_PROPERTY_U32(np, "jitter.curr_ratio",
			p_data->role->jitter_filter->curr_ratio);
	GET_PROPERTY_U32(np, "quickcover.enable",
			p_data->role->quickcover_filter->enable);
	GET_PROPERTY_U32(np, "quickcover.X1",
			p_data->role->quickcover_filter->X1);
	GET_PROPERTY_U32(np, "quickcover.X2",
			p_data->role->quickcover_filter->X2);
	GET_PROPERTY_U32(np, "quickcover.Y1",
			p_data->role->quickcover_filter->Y1);
	GET_PROPERTY_U32(np, "quickcover.Y2",
			p_data->role->quickcover_filter->Y2);
	GET_PROPERTY_U32(np, "crack.enable",
			p_data->role->crack_detection->use_crack_mode);
	GET_PROPERTY_U32(np, "crack.min.cap",
			p_data->role->crack_detection->min_cap_value);

	GET_PROPERTY_U8(np, "ghost_detection_enable",
			p_data->role->ghost_detection->check_enable.ghost_detection_enable);
	GET_PROPERTY_U8(np, "ta_noise_chk",
			p_data->role->ghost_detection->check_enable.ta_noise_chk);
	GET_PROPERTY_U8(np, "incoming_call_chk",
			p_data->role->ghost_detection->check_enable.incoming_call_chk);
	GET_PROPERTY_U8(np, "first_finger_chk",
			p_data->role->ghost_detection->check_enable.first_finger_chk);
	GET_PROPERTY_U8(np, "pressure_zero_chk",
			p_data->role->ghost_detection->check_enable.pressure_zero_chk);
	GET_PROPERTY_U8(np, "ta_debouncing_chk",
			p_data->role->ghost_detection->check_enable.ta_debouncing_chk);
	GET_PROPERTY_U8(np, "press_interval_chk",
			p_data->role->ghost_detection->check_enable.press_interval_chk);
	GET_PROPERTY_U8(np, "diff_fingers_chk",
			p_data->role->ghost_detection->check_enable.diff_fingers_chk);
	GET_PROPERTY_U8(np, "subtraction_finger_chk",
			p_data->role->ghost_detection->check_enable.subtraction_finger_chk);
	GET_PROPERTY_U8(np, "long_press_chk",
			p_data->role->ghost_detection->check_enable.long_press_chk);
	GET_PROPERTY_U8(np, "button_chk",
			p_data->role->ghost_detection->check_enable.button_chk);
	GET_PROPERTY_U8(np, "rebase_repetition_chk",
			p_data->role->ghost_detection->check_enable.rebase_repetition_chk);
	GET_PROPERTY_U32(np, "ghost_detection_chk_cnt",
			p_data->role->ghost_detection->ghost_detection_chk_cnt);
	GET_PROPERTY_U32(np, "jitter_value",
			p_data->role->ghost_detection->jitter_value);
	GET_PROPERTY_U32(np, "first_finger_time",
			p_data->role->ghost_detection->first_finger_time);
	GET_PROPERTY_U32(np, "ta_debouncing_cnt",
			p_data->role->ghost_detection->ta_debouncing_cnt);
	GET_PROPERTY_U32(np, "ta_debouncing_finger_num",
			p_data->role->ghost_detection->ta_debouncing_finger_num);
	GET_PROPERTY_U32(np, "press_interval",
			p_data->role->ghost_detection->press_interval);
	GET_PROPERTY_U32(np, "diff_finger_num",
			p_data->role->ghost_detection->diff_finger_num);
	GET_PROPERTY_U32(np, "subtraction_time",
			p_data->role->ghost_detection->subtraction_time);
	GET_PROPERTY_U32(np, "subtraction_finger_cnt",
			p_data->role->ghost_detection->subtraction_finger_cnt);
	GET_PROPERTY_U32(np, "long_press_chk_time",
			p_data->role->ghost_detection->long_press_chk_time);
	GET_PROPERTY_U32(np, "long_press_cnt",
			p_data->role->ghost_detection->long_press_cnt);
	GET_PROPERTY_U32(np, "button_int_num",
			p_data->role->ghost_detection->button_int_num);
	GET_PROPERTY_U32(np, "button_duration",
			p_data->role->ghost_detection->button_duration);
	GET_PROPERTY_U32(np, "rebase_since_init",
			p_data->role->ghost_detection->rebase_since_init);
	GET_PROPERTY_U32(np, "rebase_since_rebase",
			p_data->role->ghost_detection->rebase_since_rebase);
	// POWER
	GET_PROPERTY_U32(np, "use_regulator", p_data->pwr->use_regulator);
	GET_PROPERTY_STRING(np, "vdd", p_data->pwr->vdd);
	GET_PROPERTY_U32(np, "vdd_voltage", p_data->pwr->vdd_voltage);
	GET_PROPERTY_STRING(np, "vio", p_data->pwr->vio);
	GET_PROPERTY_U32(np, "vio_voltage", p_data->pwr->vio_voltage);
	GET_PROPERTY_U32(np, "vio_control", p_data->pwr->vio_control);
	GET_PROPERTY_U32(np, "reset_control", p_data->pwr->reset_control);

	// FIRMWARE
	GET_PROPERTY_U32(np, "need_upgrade", p_data->fw->need_upgrade);

	return p_data;

error_mem:
	return NULL;
}


/* check_platform_data
 *
 */
static int check_platform_data(const struct touch_platform_data *p_data)
{
	struct touch_device_caps *caps = p_data->caps;
	struct touch_operation_role *role = p_data->role;
	struct touch_power_module *pwr = p_data->pwr;

	// caps
	if (caps->button_support)
		ERROR_IF(caps->number_of_button <= 0
				|| caps->number_of_button > MAX_BUTTON,
				"0 < number_of_button <= MAX_BUTTON[4]\n",
				error);

	ERROR_IF(caps->max_x <= 0 || caps->max_y <= 0
			|| caps->max_pressure <= 0 || caps->max_width <= 0
			|| caps->max_orientation <= 0 || caps->max_id <= 0,
			"These information should be supported"
			"[id, x, y, pressure, width, orientation]\n", error);

	ERROR_IF(caps->max_id > MAX_FINGER,
			"0 < max_id < MAX_FINGER[10]\n", error);

	// role
	ERROR_IF(role->booting_delay <= 0 || role->reset_delay <= 0,
			"Both booting_delay and reset_delay "
			"should be defined.\n", error);

	if (role->accuracy_filter->enable) {
		ERROR_IF(role->accuracy_filter->min_delta <= 0,
				"accuracy_filter.min_delta > 0\n", error);
		ERROR_IF(role->accuracy_filter->curr_ratio < 0
				|| role->accuracy_filter->curr_ratio > 128,
				"0 <= accuracy_filter.curr_ratio <= 128\n",
				error);
		ERROR_IF(role->accuracy_filter->min_pressure < 0
				|| role->accuracy_filter->min_pressure
				> caps->max_pressure,
				"0 <= accuracy_filter.min_pressure "
				"<= max_pressure\n",
				error);
	}

	if (role->jitter_filter->enable)
		ERROR_IF(role->jitter_filter->curr_ratio < 0
				|| role->jitter_filter->curr_ratio > 128,
				"0 <= jitter_filter.curr_ratio <= 128\n",
				error);

	// power
	if (pwr->use_regulator)
		ERROR_IF(!pwr->vdd || !pwr->vio,
				"VDD, VIO should be defined"
				"if use_regulator is true\n", error);
	return 0;
error:
	return -1;
}

static int get_platform_data(struct touch_platform_data **p_data,
		struct i2c_client *client)
{
	if (client->dev.of_node)
		ASSIGN(*p_data = get_dts_data(&client->dev), error);
	else
		ASSIGN(*p_data =
				(struct touch_platform_data *)
				client->dev.platform_data, error);

	DO_SAFE(check_platform_data(*p_data), error);

	return 0;
error:
	return -EPERM;
}

/* check_specific_func
 *
 * All functions are should be implemented in specific_driver.
 */
static int check_specific_func(void)
{
	if (touch_device_func->probe && touch_device_func->remove &&
			touch_device_func->resume &&
			touch_device_func->suspend &&
			touch_device_func->init &&
			touch_device_func->data &&
			touch_device_func->filter &&
			touch_device_func->power &&
			touch_device_func->ic_ctrl &&
			touch_device_func->fw_upgrade &&
			touch_device_func->notify &&
			touch_device_func->ime_drumming &&
			touch_device_func->get_type_bl)
		return 0;
	else
		return -EPERM;
}

/* add_filter_func, remove_filter_func
 *
 */
#define ADD_NEW_FILTER(dev, _filter, head)	\
	do {							\
		if (ts->_filter.role->enable) {			\
			struct filter_func *new_filter;		\
			new_filter = devm_kzalloc(&dev, 	\
					sizeof(struct filter_func), GFP_KERNEL); \
			if (new_filter == NULL)			\
			return -EPERM;			\
			new_filter->name = #_filter;		\
			new_filter->filter = _filter;		\
			list_add(&new_filter->list, &head);	\
		}						\
	} while (0)

static int add_filter_func(struct lge_touch_data *ts)
{
	INIT_LIST_HEAD(&ts->filter_head.list);

	ADD_NEW_FILTER(ts->client->dev, bouncing_filter, ts->filter_head.list);
	ADD_NEW_FILTER(ts->client->dev, grip_filter, ts->filter_head.list);
	ADD_NEW_FILTER(ts->client->dev, accuracy_filter, ts->filter_head.list);
	ADD_NEW_FILTER(ts->client->dev, jitter_filter, ts->filter_head.list);
	ADD_NEW_FILTER(ts->client->dev, quickcover_filter,
			ts->filter_head.list);

	return 0;
}

static void remove_filter_func(struct lge_touch_data *ts)
{
	struct filter_func *tmp;
	struct list_head *pos, *n;

	list_for_each_safe(pos, n, &ts->filter_head.list) {
		tmp = list_entry(pos, struct filter_func, list);
		list_del(pos);
		devm_kfree(&ts->client->dev, tmp);
	}
}


/* sysfs_register
 *
 * get_attribute_array_size
 * : attribute_list should has NULL value at the end of list.
 */
#define MAX_ATTRIBUTE_ARRAY_SIZE 30
int get_attribute_array_size(struct attribute **list)
{
	int i = 0;

	while (list[i] != NULL)
		i++;

	return i <= MAX_ATTRIBUTE_ARRAY_SIZE ? i : 0;
}

static int sysfs_register(struct lge_touch_data *ts)
{
	int ret = 0;

	ret = subsys_system_register(&touch_subsys, NULL);
	if (ret < 0)
		TOUCH_ERR_MSG("%s, bus is not registered, ret : %d\n",
				__func__, ret);
	ret = device_register(&device_touch);
	if (ret < 0)
		TOUCH_ERR_MSG("%s, device is not registered, ret : %d\n",
				__func__, ret);

	DO_SAFE(ret = kobject_init_and_add(&ts->lge_touch_kobj,
				&lge_touch_kobj_type,
				ts->input_dev->dev.kobj.parent,
				"%s", LGE_TOUCH_NAME),
			err_kobject_init_and_add);

	ret = sysfs_create_group(&ts->lge_touch_kobj,
			&lge_touch_attribute_group);

	if (touch_device_func->register_sysfs)
		ret = touch_device_func->register_sysfs(&ts->lge_touch_kobj);

	return ret;

err_kobject_init_and_add:
	kobject_del(&ts->lge_touch_kobj);
	return ret;
}

static int touch_suspend(struct device *dev)
{
	struct lge_touch_data *ts =  dev_get_drvdata(dev);

	TOUCH_TRACE();
	TOUCH_INFO_MSG("%s : touch_suspend start\n", __func__);

	cancel_delayed_work_sync(&ts->work_init);
	cancel_delayed_work_sync(&ts->work_upgrade);
	cancel_delayed_work_sync(&ts->work_trigger_handle);

	atomic_set(&ts->state.uevent_state, UEVENT_IDLE);
	mutex_lock(&ts->pdata->thread_lock);
	touch_device_func->suspend(ts->client);
	power_control(ts, ts->pdata->role->use_sleep_mode
			? POWER_SLEEP : POWER_OFF);

	if ((quick_cover_status == QUICKCOVER_CLOSE)
			&& (atomic_read(&ts->state.power_state) != POWER_OFF)) {
		touch_device_func->lpwg(ts->client,
				LPWG_ACTIVE_AREA_X1,
				ts->pdata->role->quickcover_filter->X1, NULL);
		touch_device_func->lpwg(ts->client,
				LPWG_ACTIVE_AREA_X2,
				ts->pdata->role->quickcover_filter->X2, NULL);
		touch_device_func->lpwg(ts->client,
				LPWG_ACTIVE_AREA_Y1,
				ts->pdata->role->quickcover_filter->Y1, NULL);
		touch_device_func->lpwg(ts->client,
				LPWG_ACTIVE_AREA_Y2,
				ts->pdata->role->quickcover_filter->Y2, NULL);
	}
	mutex_unlock(&ts->pdata->thread_lock);

	TOUCH_INFO_MSG("%s : touch_suspend done\n", __func__);
	return 0;
}

static int touch_resume(struct device *dev)
{
	struct lge_touch_data *ts =  dev_get_drvdata(dev);

	TOUCH_TRACE();
	TOUCH_INFO_MSG("%s : touch_resume start\n", __func__);

	if(!boot_mode) {
		TOUCH_INFO_MSG("%s  : Ignore resume in Chargerlogo mode\n", __func__);
		return 0;
	} else
		TOUCH_INFO_MSG("%s : Normal mode\n", __func__);

	if (window_crack_check == CRACK) {
		char *window_crack[2] = {"TOUCH_WINDOW_STATE=CRACK", NULL };
		TOUCH_INFO_MSG("%s, %d : window craked - send uvent\n",
				__func__, __LINE__);
		send_uevent(&ts->client->dev, window_crack);
	}

	mutex_lock(&ts->pdata->thread_lock);

	if (ts->pdata->role->use_sleep_mode) {
		power_control(ts, POWER_OFF);
		power_control(ts, POWER_ON);
	} else {
		power_control(ts, POWER_ON);
	}

	if (atomic_read(&ts->state.upgrade_state) != UPGRADE_START) {
		touch_device_func->resume(ts->client);
	} else
		TOUCH_DEBUG(DEBUG_BASE_INFO,
				"'[Touch]%s:F/W-upgrade' is not finished.\n",
				__func__);

	TOUCH_INFO_MSG("%s : touch_resume done\n", __func__);
	mutex_unlock(&ts->pdata->thread_lock);

	if (ts->pdata->swipe_pwr_ctrl_stage == SKIP_POWER_CONTROL) {
		TOUCH_INFO_MSG("%s : Initialize without booting_delay (swipe_pwr_ctrl_stage = %d)\n",
				__func__, ts->pdata->swipe_pwr_ctrl_stage);
		ts->pdata->swipe_pwr_ctrl_stage = WAIT_TOUCH_PRESS;
		mod_delayed_work(touch_wq, &ts->work_init, msecs_to_jiffies(0));
	} else {
		mod_delayed_work(touch_wq, &ts->work_init,
				msecs_to_jiffies(ts->pdata->role->booting_delay));
	}

	return 0;
}

#if defined(CONFIG_HAS_EARLYSUSPEND)
static void touch_early_suspend(struct early_suspend *h)
{
	struct lge_touch_data *ts =
		container_of(h, struct lge_touch_data, early_suspend);

	touch_suspend(&ts->client->dev);
}

static void touch_late_resume(struct early_suspend *h)
{
	struct lge_touch_data *ts =
		container_of(h, struct lge_touch_data, early_suspend);

	touch_resume(&ts->client->dev);
}

#elif defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	struct fb_event *evdata = (struct fb_event *)data;
	struct lge_touch_data *ts =
		container_of(self, struct lge_touch_data, fb_notif);

	if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		int *blank = (int *)evdata->data;

		TOUCH_INFO_MSG("%s : event = %d\n",
				__func__, *blank);
		if (*blank == FB_BLANK_UNBLANK)
			touch_resume(&ts->client->dev);
		else if (*blank == FB_BLANK_POWERDOWN)
			touch_suspend(&ts->client->dev);
	}

	return 0;
}
#endif
static int lcd_notifier_callback(struct notifier_block *this,
		unsigned long event, void *data)
{
	struct lge_touch_data *ts =
		container_of(this, struct lge_touch_data, notif);

	TOUCH_DEBUG(DEBUG_BASE_INFO,"%s: event = %lu\n", __func__, event);
	switch (event) {
		case LCD_EVENT_TOUCH_LPWG_ON:
			TOUCH_DEBUG(DEBUG_BASE_INFO, "LCD_EVENT_TOUCH_LPWG_ON\n");
			touch_device_func->lpwg(ts_data->client, LPWG_INCELL_LPWG_ON, 0, NULL);
			break;
		case LCD_EVENT_TOUCH_LPWG_OFF:
			if(!boot_mode) {
				TOUCH_INFO_MSG("%s  : Ignore resume in Chargerlogo mode\n", __func__);
				return 0;
			}
			TOUCH_DEBUG(DEBUG_BASE_INFO, "LCD_EVENT_TOUCH_LPWG_OFF\n");
			mutex_lock(&ts->pdata->thread_lock);
			touch_device_func->lpwg(ts_data->client, LPWG_INCELL_LPWG_OFF, 0, NULL);
			mutex_unlock(&ts->pdata->thread_lock);
			break;
	}
	return 0;
}
static int touch_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct lge_touch_data *ts;
	int ret = -ENOMEM;

	TOUCH_TRACE();
	TOUCH_INFO_MSG("touch_probe start\n");

	ASSIGN(ts = devm_kzalloc(&client->dev,
				sizeof(struct lge_touch_data), GFP_KERNEL),
			error);
	DO_IF(i2c_check_functionality(client->adapter,
				I2C_FUNC_I2C) == 0, error);
	DO_SAFE(get_platform_data(&ts->pdata, client), error);

	ASSIGN(ts->client = client, error);
	i2c_set_clientdata(client, ts);

	DO_SAFE(ret = check_specific_func(), error);

	if (ts->pdata->reset_pin > 0) {
		DO_SAFE(ret = gpio_request(ts->pdata->reset_pin,
					"touch_reset"), error);
		gpio_direction_output(ts->pdata->reset_pin, 0);
	}

	if (ts->pdata->int_pin > 0) {
		DO_SAFE(ret = gpio_request(ts->pdata->int_pin,
					"touch_int"), error);
		gpio_direction_input(ts->pdata->int_pin);
	}

	memset(&ts->state, 0, sizeof(struct state_info));
	state = &ts->state;
	client_only_for_update_status = client;


	/* Get pinctrl if target uses pinctrl */
	TOUCH_INFO_MSG("Start pinctrl \n");
	ts->ts_pinctrl = devm_pinctrl_get(&(client->dev));
	if (IS_ERR(ts->ts_pinctrl)) {
		if (PTR_ERR(ts->ts_pinctrl) == -EPROBE_DEFER) {
			TOUCH_INFO_MSG("ts_pinctrl ==  -EPROBE_DEFER\n");
			return -EPROBE_DEFER;
		}
		TOUCH_INFO_MSG("Target does not use pinctrl(ts->ts_pinctrl == NULL) \n");
		ts->ts_pinctrl = NULL;
	}

	if (ts->ts_pinctrl) {
		ts->ts_pinset_state_active = pinctrl_lookup_state(ts->ts_pinctrl, "pmx_ts_active");
		if (IS_ERR(ts->ts_pinset_state_active))
			TOUCH_ERR_MSG("cannot get ts pinctrl active state\n");

		ts->ts_pinset_state_suspend = pinctrl_lookup_state(ts->ts_pinctrl, "pmx_ts_suspend");
		if (IS_ERR(ts->ts_pinset_state_suspend))
			TOUCH_ERR_MSG("cannot get ts pinctrl active state\n");

		if (ts->ts_pinset_state_active) {
			ret = pinctrl_select_state(ts->ts_pinctrl, ts->ts_pinset_state_active);
			if (ret)
				TOUCH_INFO_MSG("cannot set ts pinctrl active state \n");
		} else {
			TOUCH_INFO_MSG("pinctrl active == NULL \n");
		}
	}
	TOUCH_INFO_MSG("End pinctrl \n");

	DO_IF(touch_device_func->probe(ts->client, ts->pdata,
				&ts->state) != 0,
			error);

	DO_SAFE(power_control(ts, POWER_ON), error);

	msleep(ts->pdata->role->booting_delay);

	ts->notif.notifier_call = lcd_notifier_callback;
	if (touch_register_client(&ts->notif) != 0) {
		TOUCH_ERR_MSG("Failed to register fb callback\n");
		ret = -EINVAL;
		goto error;
	}

	// it should be initialized before both init_func and enable_irq
	mutex_init(&ts->pdata->thread_lock);
	INIT_DELAYED_WORK(&ts->work_init, touch_init_func);
	INIT_DELAYED_WORK(&ts->work_upgrade, firmware_upgrade_func);
	INIT_DELAYED_WORK(&ts->work_ime_drumming, change_ime_drumming_func);
	INIT_DELAYED_WORK(&ts->work_trigger_handle, touch_trigger_handle);
	INIT_DELAYED_WORK(&ts->work_thermal, change_thermal_param);
	INIT_DELAYED_WORK(&ts->work_crack, inspection_crack_func);
	INIT_DELAYED_WORK(&ts->work_sleepmode, sleep_mode_func);

	ASSIGN(ts->input_dev = input_allocate_device(),
			err_input_allocate_device);
	ts->input_dev->name = "touch_dev";

	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);

	input_set_abs_params(ts->input_dev,
			ABS_MT_POSITION_X, 0, ts->pdata->caps->max_x, 0, 0);
	input_set_abs_params(ts->input_dev,
			ABS_MT_POSITION_Y, 0, ts->pdata->caps->max_y, 0, 0);
	input_set_abs_params(ts->input_dev,
			ABS_MT_PRESSURE, 0,
			ts->pdata->caps->max_pressure, 0, 0);
	input_set_abs_params(ts->input_dev,
			ABS_MT_WIDTH_MAJOR, 0,
			ts->pdata->caps->max_width, 0, 0);
	input_set_abs_params(ts->input_dev,
			ABS_MT_WIDTH_MINOR, 0,
			ts->pdata->caps->max_width, 0, 0);
	input_set_abs_params(ts->input_dev,
			ABS_MT_ORIENTATION, 0,
			ts->pdata->caps->max_orientation, 0, 0);

	if (ts->pdata->role->protocol_type == MT_PROTOCOL_A)
		input_set_abs_params(ts->input_dev,
				ABS_MT_TRACKING_ID, 0,
				ts->pdata->caps->max_id, 0, 0);
	/* INPUT_MT_DIRECT */
	else
		DO_SAFE(input_mt_init_slots(ts->input_dev,
					ts->pdata->caps->max_id, 0),
				err_mt_init_slots);

	if (ts->pdata->caps->button_support) {
		set_bit(EV_KEY, ts->input_dev->evbit);
		for (ret = 0; ret < ts->pdata->caps->number_of_button; ret++)
			set_bit(ts->pdata->caps->button_name[ret],
					ts->input_dev->keybit);
	}

	DO_SAFE(ret = input_register_device(ts->input_dev),
			err_input_register_device);

	if (touch_ic_init(ts, 0) < 0) {
		touch_device_func->remove(ts->client);
		ret = -ENOMEM;
		goto err_ic_init;
	}

	DO_SAFE(ret = request_threaded_irq(ts->client->irq,
				touch_irq_handler, touch_thread_irq_handler,
				ts->pdata->role->irqflags | IRQF_ONESHOT,
				ts->client->name, ts),
			err_request_threaded_irq);
	interrupt_control(ts, INTERRUPT_ENABLE);

        queue_delayed_work(touch_wq, &ts->work_upgrade, 0);

	if (ts->pdata->role->crack_detection->use_crack_mode)
		queue_delayed_work(touch_wq, &ts->work_crack,
				msecs_to_jiffies(100));

	ts->bouncing_filter.role = ts->pdata->role->bouncing_filter;
	ts->grip_filter.role = ts->pdata->role->grip_filter;
	ts->accuracy_filter.role = ts->pdata->role->accuracy_filter;
	ts->jitter_filter.role = ts->pdata->role->jitter_filter;
	ts->quickcover_filter.role = ts->pdata->role->quickcover_filter;
	DO_SAFE(ret = add_filter_func(ts), err_request_threaded_irq);

	DO_SAFE(ret = sysfs_register(ts), err_request_threaded_irq);
	wake_lock_init(&ts->lpwg_wake_lock, WAKE_LOCK_SUSPEND, "touch_lpwg");

	input_set_drvdata(ts->input_dev, ts);

#if defined(CONFIG_HAS_EARLYSUSPEND)
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = touch_early_suspend;
	ts->early_suspend.resume = touch_late_resume;
	register_early_suspend(&ts->early_suspend);
#elif defined(CONFIG_FB)
	ts->fb_notif.notifier_call = fb_notifier_callback;
	fb_register_client(&ts->fb_notif);
#endif
	ts_data = ts;

	factory_boot = lge_get_factory_boot();
	TOUCH_INFO_MSG("factory boot check : %d\n", factory_boot);

	is_probe = 1;
	TOUCH_INFO_MSG("touch_probe done\n");

	if (ts->pdata->role->use_lcd_notifier_callback)
		touch_notifier_call_chain(LCD_EVENT_TOUCH_DRIVER_REGISTERED, NULL);

	return 0;

err_request_threaded_irq:
	free_irq(ts->client->irq, ts);

err_ic_init:
	if (ts->pdata->role->protocol_type == MT_PROTOCOL_B)
		input_mt_destroy_slots(ts->input_dev);

	input_unregister_device(ts->input_dev);
	TOUCH_ERR_MSG("probe failed\n");

	return ret;

err_input_register_device:
	if (ts->pdata->role->protocol_type == MT_PROTOCOL_B)
		input_mt_destroy_slots(ts->input_dev);

err_mt_init_slots:
	input_free_device(ts->input_dev);

err_input_allocate_device:
error:
	TOUCH_ERR_MSG("probe failed\n");
	return ret;
}

static int touch_remove(struct i2c_client *client)
{
	struct lge_touch_data *ts = i2c_get_clientdata(client);

	TOUCH_TRACE();

#if defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&ts->early_suspend);
#elif defined(CONFIG_FB)
	fb_unregister_client(&ts->fb_notif);
#endif

	kobject_del(&ts->lge_touch_kobj);

	wake_lock_destroy(&ts->lpwg_wake_lock);
	remove_filter_func(ts);

	interrupt_control(ts, INTERRUPT_DISABLE);
	free_irq(ts->client->irq, ts);

	if (ts->pdata->role->protocol_type == MT_PROTOCOL_B)
		input_mt_destroy_slots(ts->input_dev);

	touch_device_func->remove(ts->client);
	power_control(ts, POWER_OFF);

	input_unregister_device(ts->input_dev);
	devm_kfree(&ts->client->dev, ts);

	return 0;
}
static int touch_pm_suspend(struct device *dev)
{
	struct lge_touch_data *ts = dev_get_drvdata(dev);

	TOUCH_TRACE();
	atomic_set(&ts->state.pm_state, PM_SUSPEND);
	TOUCH_INFO_MSG("%s : PM_SUSPEND\n", __func__);

	return 0;
}

static int touch_pm_resume(struct device *dev)
{
	struct lge_touch_data *ts = dev_get_drvdata(dev);

	TOUCH_TRACE();

	if (atomic_read(&ts->state.pm_state) == PM_SUSPEND_IRQ) {
		struct irq_desc *desc = irq_to_desc(ts->client->irq);

		if (desc == NULL) {
			TOUCH_DEBUG(DEBUG_BASE_INFO,
					"Null Pointer from irq_to_desc"
					"(ts->client->irq)\n");
			return -ENOMEM;
		}

		atomic_set(&ts->state.pm_state, PM_RESUME);

		irq_set_pending(ts->client->irq);
		check_irq_resend(desc, ts->client->irq);

		TOUCH_DEBUG(DEBUG_BASE_INFO, "resend interrupt");

		return 0;
	}

	atomic_set(&ts->state.pm_state, PM_RESUME);
	TOUCH_INFO_MSG("%s : PM_RESUME\n", __func__);

	return 0;
}

static struct dev_pm_ops touch_pm_ops = {
#if (!defined(CONFIG_FB) && !defined(CONFIG_HAS_EARLYSUSPEND))
	.suspend = touch_suspend,
	.resume = touch_resume,
#endif
	.suspend = touch_pm_suspend,
	.resume = touch_pm_resume,
};


static struct i2c_device_id lge_ts_id[] = {
	{LGE_TOUCH_NAME, 0 },
};

static struct i2c_driver lge_touch_driver = {
	.probe   = touch_probe,
	.remove	 = touch_remove,
	.id_table = lge_ts_id,
	.driver	 = {
		.name   = LGE_TOUCH_NAME,
		.owner	= THIS_MODULE,
		.pm	= &touch_pm_ops,
	},
};

int touch_driver_register(struct touch_device_driver *driver,
		struct of_device_id *match_table)
{
	int ret = 0;
	TOUCH_TRACE();

	touch_device_func = driver;
	ASSIGN(touch_wq = create_singlethread_workqueue("touch_wq"),
			err_create_workqueue);
	lge_touch_driver.driver.of_match_table = match_table;
	DO_SAFE(ret = i2c_add_driver(&lge_touch_driver), err_i2c_add_driver);
	return 0;

err_i2c_add_driver:
	destroy_workqueue(touch_wq);
	TOUCH_TRACE();
err_create_workqueue:
	return ret;
}

void touch_driver_unregister(void)
{
	TOUCH_TRACE();

	i2c_del_driver(&lge_touch_driver);
	touch_device_func = NULL;

	if (touch_wq)
		destroy_workqueue(touch_wq);
}

