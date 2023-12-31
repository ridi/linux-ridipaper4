/*
 driver/usbpd/s2mu106.c - s2mu106 USB PD(Power Delivery) device driver
 *
 * Copyright (C) 2018 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/completion.h>

#include <linux/ccic/usbpd.h>
#include <linux/ccic/usbpd-s2mu106.h>

#if defined(CONFIG_PM_S2MU106)
#include <linux/power/s2mu106_pmeter.h>
#endif

#include <linux/muic/muic.h>
#if defined(CONFIG_MUIC_NOTIFIER)
#include <linux/muic/muic_notifier.h>
#endif /* CONFIG_MUIC_NOTIFIER */

#ifdef CONFIG_BATTERY_SAMSUNG
#include <linux/sec_batt.h>
#include <linux/battery/sec_charging_common.h>
#else
#include <linux/power_supply.h>
#endif
#if defined(CONFIG_USB_HOST_NOTIFY) || defined(CONFIG_USB_HW_PARAM)
#include <linux/usb_notify.h>
#endif
#include <linux/regulator/consumer.h>

#if (defined CONFIG_IFCONN_NOTIFIER || defined CONFIG_DUAL_ROLE_USB_INTF)
#include <linux/ccic/usbpd_ext.h>
#endif
#if defined CONFIG_IFCONN_NOTIFIER
struct pdic_notifier_data pd_noti;
#endif

#if defined(CONFIG_MACH_RP400)
extern int get_epd_usb_role(void);
extern int get_rp400_usb_lock(void);
#endif

/*
*VARIABLE DEFINITION
*/
static usbpd_phy_ops_type s2mu106_ops;
struct i2c_client *test_i2c;

static enum power_supply_property ccic_props[] = {
};

static char *ccic_supplied_to[] = {
	"battery",
};
/*
*FUNCTION DEFINITION
*/
static int s2mu106_receive_message(void *data);
static int s2mu106_check_port_detect(struct s2mu106_usbpd_data *pdic_data);
static int s2mu106_usbpd_reg_init(struct s2mu106_usbpd_data *_data);
static void s2mu106_dfp(struct i2c_client *i2c);
static void s2mu106_ufp(struct i2c_client *i2c);
static int s2mu106_usbpd_check_msg(void *_data, u64 *val);
static void s2mu106_src(struct i2c_client *i2c);
static void s2mu106_snk(struct i2c_client *i2c);
static void s2mu106_assert_rd(void *_data);
static void s2mu106_assert_rp(void *_data);
static void s2mu106_assert_drp(void *_data);
static void s2mu106_usbpd_check_rid(struct s2mu106_usbpd_data *pdic_data);
static int s2mu106_usbpd_read_reg(struct i2c_client *i2c, u8 reg, u8 *dest);
static int s2mu106_usbpd_write_reg(struct i2c_client *i2c, u8 reg, u8 value);
#ifndef CONFIG_SEC_FACTORY
static void s2mu106_usbpd_notify_detach(struct s2mu106_usbpd_data *pdic_data);
#endif
static void s2mu106_usbpd_detach_init(struct s2mu106_usbpd_data *pdic_data);
static int s2mu106_usbpd_set_cc_control(struct s2mu106_usbpd_data  *pdic_data, int val);
static void s2mu106_usbpd_set_rp_scr_sel(struct s2mu106_usbpd_data *pdic_data,
							CCIC_RP_SCR_SEL scr_sel);

char *rid_text[] = {
	"UNDEFINED",
	"RID ERROR",
	"RID ERROR",
	"RID 255K",
	"RID 301K",
	"RID 523K",
	"RID 619K"
};

static void s2mu106_usbpd_test_read(struct s2mu106_usbpd_data *usbpd_data)
{
	struct i2c_client *i2c = usbpd_data->i2c;
	u8 data[10];

	s2mu106_usbpd_read_reg(i2c, 0x1, &data[0]);
	s2mu106_usbpd_read_reg(i2c, 0x18, &data[1]);
	s2mu106_usbpd_read_reg(i2c, 0x27, &data[2]);
	s2mu106_usbpd_read_reg(i2c, 0x28, &data[3]);
	s2mu106_usbpd_read_reg(i2c, 0x40, &data[4]);
	s2mu106_usbpd_read_reg(i2c, 0xe2, &data[5]);
	s2mu106_usbpd_read_reg(i2c, 0xb3, &data[6]);
	s2mu106_usbpd_read_reg(i2c, 0xb4, &data[7]);
	s2mu106_usbpd_read_reg(i2c, 0xf7, &data[8]);

	pr_info("%s, 0x1(%x) 0x18(%x) 0x27(%x) 0x28(%x) 0x40(%x) 0xe2(%x) 0xb3(%x) 0xb4(%x) 0xf7(%X)\n",
					__func__, data[0], data[1], data[2], data[3], data[4],
										data[5], data[6], data[7], data[8]);
}

void s2mu106_rprd_mode_change(struct s2mu106_usbpd_data *usbpd_data, u8 mode)
{
	u8 data = 0;
	struct i2c_client *i2c = usbpd_data->i2c;
	struct device *dev = &i2c->dev;
	struct usbpd_data *pd_data = dev_get_drvdata(dev);
	u64 one = 1;

	pr_info("%s, mode=0x%x\n", __func__, mode);

	mutex_lock(&usbpd_data->_mutex);
	if (usbpd_data->lpm_mode)
		goto skip;

	pr_info("%s, %d\n", __func__, __LINE__);
	switch (mode) {
	case TYPE_C_ATTACH_DFP: /* SRC */
		s2mu106_usbpd_set_cc_control(usbpd_data, USBPD_CC_OFF);
		s2mu106_usbpd_set_rp_scr_sel(usbpd_data, PLUG_CTRL_RP0);
		s2mu106_assert_rp(pd_data);
		msleep(20);
		s2mu106_usbpd_detach_init(usbpd_data);
		s2mu106_usbpd_notify_detach(usbpd_data);
		msleep(600);
		s2mu106_usbpd_set_rp_scr_sel(usbpd_data, PLUG_CTRL_RP80);
		msleep(S2MU106_ROLE_SWAP_TIME_MS);
		s2mu106_assert_drp(pd_data);
//		usbpd_data->status_reg |= PLUG_ATTACH;
		usbpd_data->status_reg |= one << PLUG_ATTACH;
		schedule_delayed_work(&usbpd_data->plug_work, 0);
		break;
	case TYPE_C_ATTACH_UFP: /* SNK */
		ifconn_event_work(usbpd_data, IFCONN_NOTIFY_MUIC,
			IFCONN_NOTIFY_ID_ROLE_SWAP,
			IFCONN_NOTIFY_EVENT_PD_SINK, NULL);
		s2mu106_usbpd_set_cc_control(usbpd_data, USBPD_CC_OFF);
		s2mu106_assert_rp(pd_data);
		s2mu106_usbpd_set_rp_scr_sel(usbpd_data, PLUG_CTRL_RP0);
		msleep(20);
		s2mu106_usbpd_detach_init(usbpd_data);
		s2mu106_usbpd_notify_detach(usbpd_data);
		msleep(600);
		s2mu106_assert_rd(pd_data);
		s2mu106_usbpd_set_rp_scr_sel(usbpd_data, PLUG_CTRL_RP80);
		msleep(S2MU106_ROLE_SWAP_TIME_MS);
		s2mu106_assert_drp(pd_data);
//		usbpd_data->status_reg |= PLUG_ATTACH;
		usbpd_data->status_reg |= one << PLUG_ATTACH;
		schedule_delayed_work(&usbpd_data->plug_work, 0);
		break;
	case TYPE_C_ATTACH_DRP: /* DRP */
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, &data);
		data |= S2MU106_REG_PLUG_CTRL_DRP;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, data);
		break;
	};
skip:
	mutex_unlock(&usbpd_data->_mutex);
}

void usbpd_charger_test_read(struct s2mu106_usbpd_data *usbpd_data)
{
	struct power_supply *psy_charger;
	union power_supply_propval val;
	int ret = 0;

	psy_charger = power_supply_get_by_name("s2mu106-charger");

	if (psy_charger) {
		ret = psy_charger->desc->set_property(psy_charger,
							POWER_SUPPLY_PROP_USBPD_TEST_READ, &val);
	} else {
		pr_err("%s: Fail to get psy battery\n", __func__);

		return;
	}
}

void vbus_turn_on_ctrl(struct s2mu106_usbpd_data *usbpd_data, bool enable)
{
	struct power_supply *psy_otg;
	union power_supply_propval val;
	int on = !!enable;
	int ret = 0, retry_cnt = 0;

	pr_info("%s %d, enable=%d\n", __func__, __LINE__, enable);

	if (usbpd_data->detach_valid) {
		dev_info(usbpd_data->dev, "%s, ignore vbus control\n", __func__);
		return;
	}

	psy_otg = power_supply_get_by_name("otg");

	if (psy_otg) {
		val.intval = enable;
		usbpd_data->is_otg_vboost = enable;
		ret = psy_otg->desc->set_property(psy_otg, POWER_SUPPLY_PROP_ONLINE, &val);
	} else {
		pr_err("%s: Fail to get psy battery\n", __func__);

		return;
	}
	if (ret) {
		pr_err("%s: fail to set power_suppy ONLINE property(%d)\n",
			__func__, ret);
	} else {
		if (enable == VBUS_ON) {
			for (retry_cnt = 0; retry_cnt < 5; retry_cnt++) {
				psy_otg->desc->get_property(psy_otg, POWER_SUPPLY_PROP_ONLINE, &val);
				if (val.intval == VBUS_OFF) {
					msleep(100);
					val.intval = enable;
					psy_otg->desc->set_property(psy_otg, POWER_SUPPLY_PROP_ONLINE, &val);
				} else
					break;
			}
		}
		pr_info("otg accessory power = %d\n", on);
	}

}

#ifdef CONFIG_PM_S2MU106
static void s2mu106_usbpd_set_pmeter_mode(struct s2mu106_usbpd_data *pdic_data,
																int mode)
{
	struct power_supply *psy_pm = pdic_data->psy_pm;
	union power_supply_propval val;
	int ret = 0;

	pr_info("%s, mode=%d\n", __func__, mode);

	if (psy_pm) {
		val.intval = mode;
		ret = psy_pm->desc->set_property(psy_pm,
							POWER_SUPPLY_PROP_CO_ENABLE, &val);
	} else {
		pr_err("%s: Fail to get pmeter\n", __func__);
		return;
	}

	if (ret) {
		pr_err("%s: Fail to set pmeter\n", __func__);
		return;
	}
}

static int s2mu106_usbpd_get_pmeter_volt(struct s2mu106_usbpd_data *pdic_data)
{

	struct power_supply *psy_pm = pdic_data->psy_pm;
	union power_supply_propval val;
	int ret = 0;

	if (psy_pm)
		ret = psy_pm->desc->get_property(psy_pm, POWER_SUPPLY_PROP_VCHGIN, &val);
	else {
		pr_err("%s: Fail to get pmeter\n", __func__);
		return -1;
	}

	if (ret) {
			pr_err("%s: fail to set power_suppy pmeter property(%d)\n",
		__func__, ret);
		return -1;
	}

	pdic_data->pm_chgin = val.intval;

	return 0;
}

static int s2mu106_usbpd_get_pmeter_current(struct s2mu106_usbpd_data *pdic_data)
{

	struct power_supply *psy_pm = pdic_data->psy_pm;
	union power_supply_propval val;
	int ret = 0;

	if (psy_pm)
		psy_pm->desc->get_property(psy_pm, POWER_SUPPLY_PROP_ICHGIN, &val);
	else {
		pr_err("%s: Fail to get pmeter\n", __func__);
		return -1;
	}

	if (ret) {
			pr_err("%s: fail to set power_suppy pmeter property(%d)\n",
		__func__, ret);
		return -1;
	}

	pdic_data->pm_chgin_i = val.intval;

	return 0;
}

static int s2mu106_usbpd_check_vbus(struct s2mu106_usbpd_data *pdic_data,
												int volt, CCIC_VBUS_SEL mode)
{
	int ret = 0;

	if (mode == VBUS_OFF) {
		ret = s2mu106_usbpd_get_pmeter_volt(pdic_data);
		if (ret < 0)
			return ret;

		if (pdic_data->pm_chgin < volt) {
			pr_info("%s chgin volt(%d) finish!\n", __func__,
											pdic_data->pm_chgin);
			return true;
		} else {
			pr_info("%s chgin volt(%d) waiting 400ms!\n",
									__func__, pdic_data->pm_chgin);
			msleep(400);
			return true;
		}
	} else if (mode == VBUS_ON) {
		ret = s2mu106_usbpd_get_pmeter_volt(pdic_data);
		if (ret < 0)
			return ret;
		if (pdic_data->pm_chgin > volt) {
			pr_info("%s vbus volt(%d->%d) mode(%d)!\n",
					__func__, volt, pdic_data->pm_chgin, mode);
			return true;
		} else
			return false;
	}

	pr_info("%s failed check vbus volt(%d->%d) mode(%d)!\n",
					__func__, volt, pdic_data->pm_chgin, mode);

	return false;
}
#endif

#if !defined(CONFIG_MACH_RP400)
static int s2mu106_usbpd_check_accessory(struct s2mu106_usbpd_data *pdic_data)
{
	struct i2c_client *i2c = pdic_data->i2c;
	u8 val, cc1_val, cc2_val;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_MON1, &val);

	cc1_val = val & S2MU106_REG_CTRL_MON_CC1_MASK;
	cc2_val = (val & S2MU106_REG_CTRL_MON_CC2_MASK) >> S2MU106_REG_CTRL_MON_CC2_SHIFT;

	if ((cc1_val == USBPD_Rd && cc2_val == USBPD_Rd) ||
			(cc1_val == USBPD_Ra && cc2_val == USBPD_Ra))
		return -1;

	return 0;
}
#endif

#if defined(CONFIG_IFCONN_NOTIFIER)
static void process_dr_swap(struct s2mu106_usbpd_data *usbpd_data)
{
	schedule_delayed_work(&usbpd_data->dr_work, msecs_to_jiffies(0));
}
#endif

static void s2mu106_pr_swap(void *_data, int val)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;

	if (val == USBPD_SINK_OFF) {
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_MUIC,
								IFCONN_NOTIFY_ID_ROLE_SWAP,
									IFCONN_NOTIFY_EVENT_PD_SOURCE, 0);
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_BATTERY,
								IFCONN_NOTIFY_ID_DETACH, 0, 0);
	} else if (val == USBPD_SOURCE_ON) {
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
		pdic_data->power_role_dual = DUAL_ROLE_PROP_PR_SRC;
#elif defined(CONFIG_TYPEC)
		pdic_data->typec_power_role = TYPEC_SOURCE;
#endif
	} else if (val == USBPD_SOURCE_OFF) {
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_BATTERY,
								IFCONN_NOTIFY_ID_DETACH, 0, 0);
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
		pdic_data->power_role_dual = DUAL_ROLE_PROP_PR_SNK;
#elif defined(CONFIG_TYPEC)
		pdic_data->typec_power_role = TYPEC_SINK;
#endif
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_MUIC,
								IFCONN_NOTIFY_ID_ROLE_SWAP,
									IFCONN_NOTIFY_EVENT_PD_SINK, 0);
	}
#if defined(CONFIG_TYPEC)
	if (val == USBPD_PR_DONE) {
		typec_set_pwr_role(pdic_data->port, pdic_data->typec_power_role);
		if (pdic_data->typec_try_state_change == TYPE_C_PR_SWAP)
			complete(&pdic_data->role_reverse_completion);
	}
#endif
}

static int s2mu106_usbpd_read_reg(struct i2c_client *i2c, u8 reg, u8 *dest)
{
	int ret;
	struct device *dev = &i2c->dev;
#if defined(CONFIG_USB_HW_PARAM)
	struct otg_notify *o_notify = get_otg_notify();
#endif

	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret < 0) {
		dev_err(dev, "%s reg(0x%x), ret(%d)\n", __func__, reg, ret);
#if defined(CONFIG_USB_HW_PARAM)
		if (o_notify)
			inc_hw_param(o_notify, USB_CCIC_I2C_ERROR_COUNT);
#endif
		return ret;
	}
	ret &= 0xff;
	*dest = ret;
	return 0;
}

static int s2mu106_usbpd_bulk_read(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	int ret;
	struct device *dev = &i2c->dev;
#if defined(CONFIG_USB_HW_PARAM)
	struct otg_notify *o_notify = get_otg_notify();
#endif
#ifdef CONFIG_SEC_FACTORY
	int retry = 0;
#endif

	ret = i2c_smbus_read_i2c_block_data(i2c, reg, count, buf);
#ifdef CONFIG_SEC_FACTORY
	for (retry = 0; retry < 5; retry++) {
		if (ret < 0) {
			dev_err(dev, "%s reg(0x%x), ret(%d) retry(%d) after now\n",
							__func__, reg, ret, retry);
			msleep(40);
			ret = i2c_smbus_read_i2c_block_data(i2c, reg, count, buf);
		} else
			break;
	}

	if (ret < 0) {
		dev_err(dev, "%s failed to read reg, ret(%d)\n", __func__, ret);
#else
	if (ret < 0) {
		dev_err(dev, "%s reg(0x%x), ret(%d)\n", __func__, reg, ret);
#endif

#if defined(CONFIG_USB_HW_PARAM)
		if (o_notify)
			inc_hw_param(o_notify, USB_CCIC_I2C_ERROR_COUNT);
#endif
		return ret;
	}

	return 0;
}

static int s2mu106_usbpd_write_reg(struct i2c_client *i2c, u8 reg, u8 value)
{
	int ret;
	struct device *dev = &i2c->dev;
#if defined(CONFIG_USB_HW_PARAM)
	struct otg_notify *o_notify = get_otg_notify();
#endif

	ret = i2c_smbus_write_byte_data(i2c, reg, value);
	if (ret < 0) {
		dev_err(dev, "%s reg(0x%x), ret(%d)\n", __func__, reg, ret);
#if defined(CONFIG_USB_HW_PARAM)
		if (o_notify)
			inc_hw_param(o_notify, USB_CCIC_I2C_ERROR_COUNT);
#endif
	}
	return ret;
}

static int s2mu106_usbpd_bulk_write(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	int ret;
	struct device *dev = &i2c->dev;
#if defined(CONFIG_USB_HW_PARAM)
	struct otg_notify *o_notify = get_otg_notify();
#endif

	ret = i2c_smbus_write_i2c_block_data(i2c, reg, count, buf);
	if (ret < 0) {
		dev_err(dev, "%s reg(0x%x), ret(%d)\n", __func__, reg, ret);
#if defined(CONFIG_USB_HW_PARAM)
		if (o_notify)
			inc_hw_param(o_notify, USB_CCIC_I2C_ERROR_COUNT);
#endif
		return ret;
	}
	return 0;
}

static int s2mu106_usbpd_update_bit(struct i2c_client *i2c,
			u8 reg, u8 mask, u8 shift, u8 value)
{
	int ret;
	u8 reg_val = 0;

	ret = s2mu106_usbpd_read_reg(i2c, reg, &reg_val);
	if (ret < 0) {
		pr_err("%s: Reg = 0x%X, val = 0x%X, read err : %d\n",
			__func__, reg, reg_val, ret);
	}
	reg_val &= ~mask;
	reg_val |= value << shift;
	ret = s2mu106_usbpd_write_reg(i2c, reg, reg_val);
	if (ret < 0) {
		pr_err("%s: Reg = 0x%X, mask = 0x%X, val = 0x%X, write err : %d\n",
			__func__, reg, mask, value, ret);
	}

	return ret;
}

static int s2mu106_write_msg_all(struct i2c_client *i2c, int count, u8 *buf)
{
	int ret;

	ret = s2mu106_usbpd_bulk_write(i2c, S2MU106_REG_MSG_TX_HEADER_L,
												2 + (count * 4), buf);

	return ret;
}
#if 0
static int s2mu106_write_msg_header(struct i2c_client *i2c, u8 *buf)
{
	int ret;

	ret = s2mu106_usbpd_bulk_write(i2c, S2MU106_REG_MSG_TX_HEADER_L, 2, buf);

	return ret;
}

static int s2mu106_write_msg_obj(struct i2c_client *i2c, int count, data_obj_type *obj)
{
	int ret = 0;
	int i = 0;
	struct device *dev = &i2c->dev;

	if (count > S2MU106_MAX_NUM_MSG_OBJ)
		dev_err(dev, "%s, not invalid obj count number\n", __func__);
	else
		for (i = 0; i < count; i++) {
			ret = s2mu106_usbpd_bulk_write(i2c,
				S2MU106_REG_MSG_TX_OBJECT0_0_L + (4 * i),
							4, obj[i].byte);
		}

	return ret;
}
#endif
static int s2mu106_send_msg(struct i2c_client *i2c)
{
	int ret;
	u8 reg = S2MU106_REG_MSG_SEND_CON;
	u8 val = S2MU106_REG_MSG_SEND_CON_OP_MODE
			| S2MU106_REG_MSG_SEND_CON_SEND_MSG_EN;

	s2mu106_usbpd_write_reg(i2c, reg, val);

	ret = s2mu106_usbpd_write_reg(i2c, reg, S2MU106_REG_MSG_SEND_CON_OP_MODE |
										S2MU106_REG_MSG_SEND_CON_HARD_EN);

	return ret;
}

static int s2mu106_read_msg_header(struct i2c_client *i2c, msg_header_type *header)
{
	int ret;

	ret = s2mu106_usbpd_bulk_read(i2c, S2MU106_REG_MSG_RX_HEADER_L, 2, header->byte);

	return ret;
}

static int s2mu106_read_msg_obj(struct i2c_client *i2c, int count, data_obj_type *obj)
{
	int ret = 0;
	int i = 0;
	struct device *dev = &i2c->dev;

	if (count > S2MU106_MAX_NUM_MSG_OBJ) {
		dev_err(dev, "%s, not invalid obj count number\n", __func__);
		ret = -EINVAL; /*TODO: check fail case */
	} else {
		for (i = 0; i < count; i++) {
			ret = s2mu106_usbpd_bulk_read(i2c,
				S2MU106_REG_MSG_RX_OBJECT0_0_L + (4 * i),
							4, obj[i].byte);
		}
	}

	return ret;
}

static void s2mu106_set_irq_enable(struct s2mu106_usbpd_data *_data,
		u8 int0, u8 int1, u8 int2, u8 int3, u8 int4, u8 int5)
{
	u8 int_mask[S2MU106_MAX_NUM_INT_STATUS]
		= {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	int ret = 0;
	struct i2c_client *i2c = _data->i2c;
	struct device *dev = &i2c->dev;

	int_mask[0] &= ~int0;
	int_mask[1] &= ~int1;
	int_mask[2] &= ~int2;
	int_mask[3] &= ~int3;
	int_mask[4] &= ~int4;
	int_mask[5] &= ~int5;

	ret = i2c_smbus_write_i2c_block_data(i2c, S2MU106_REG_INT_MASK0,
			S2MU106_MAX_NUM_INT_STATUS, int_mask);

	if (ret < 0)
		dev_err(dev, "err write interrupt mask \n");
}

static void s2mu106_self_soft_reset(struct i2c_client *i2c)
{
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_ETC,
			S2MU106_REG_ETC_SOFT_RESET_EN);
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_ETC,
			S2MU106_REG_ETC_SOFT_RESET_DIS);
}

static void s2mu106_driver_reset(void *_data)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	int i;

	pdic_data->status_reg = 0;
	data->wait_for_msg_arrived = 0;
	pdic_data->header.word = 0;
	for (i = 0; i < S2MU106_MAX_NUM_MSG_OBJ; i++)
		pdic_data->obj[i].object = 0;

	s2mu106_set_irq_enable(pdic_data, ENABLED_INT_0, ENABLED_INT_1,
			ENABLED_INT_2, ENABLED_INT_3, ENABLED_INT_4, ENABLED_INT_5);
}

static void s2mu106_assert_drp(void *_data)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	struct i2c_client *i2c = pdic_data->i2c;
	u8 val;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, &val);
	val &= ~S2MU106_REG_PLUG_CTRL_FSM_MANUAL_EN;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, val);

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, &val);
	val &= ~S2MU106_REG_PLUG_CTRL_FSM_MANUAL_INPUT_MASK;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, val);
}

static void s2mu106_assert_rd(void *_data)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	struct i2c_client *i2c = pdic_data->i2c;
	u8 val;
#if 0
	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_MON1, &val);

	cc1_val = val & S2MU106_REG_CTRL_MON_CC1_MASK;
	cc2_val = (val & S2MU106_REG_CTRL_MON_CC2_MASK) >> S2MU106_REG_CTRL_MON_CC2_SHIFT;
#endif

	if (pdic_data->cc1_val == USBPD_Rd) {
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, &val);
		val = (val & ~S2MU106_REG_PLUG_CTRL_CC_MANUAL_MASK) |
				S2MU106_REG_PLUG_CTRL_CC1_MANUAL_ON;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, val);

		if (pdic_data->vconn_en) {
			s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, &val);
			val = (val & ~S2MU106_REG_PLUG_CTRL_CC_MANUAL_MASK) |
					S2MU106_REG_PLUG_CTRL_RpRd_CC2_VCONN |
					S2MU106_REG_PLUG_CTRL_VCONN_MANUAL_EN;
			s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, val);
		}
	}

	if (pdic_data->cc2_val == USBPD_Rd) {
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, &val);
		val = (val & ~S2MU106_REG_PLUG_CTRL_CC_MANUAL_MASK) |
				S2MU106_REG_PLUG_CTRL_CC2_MANUAL_ON;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, val);

		if (pdic_data->vconn_en) {
			s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, &val);
			val = (val & ~S2MU106_REG_PLUG_CTRL_CC_MANUAL_MASK) |
					S2MU106_REG_PLUG_CTRL_RpRd_CC1_VCONN |
					S2MU106_REG_PLUG_CTRL_VCONN_MANUAL_EN;
			s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, val);
		}
	}

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, &val);
	val &= ~S2MU106_REG_PLUG_CTRL_FSM_MANUAL_INPUT_MASK;
	val |= S2MU106_REG_PLUG_CTRL_FSM_ATTACHED_SNK;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, val);

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, &val);
	val |= S2MU106_REG_PLUG_CTRL_FSM_MANUAL_EN;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, val);
}

static void s2mu106_assert_rp(void *_data)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	struct i2c_client *i2c = pdic_data->i2c;
	u8 val;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, &val);
	val &= ~S2MU106_REG_PLUG_CTRL_FSM_MANUAL_INPUT_MASK;
	val |= S2MU106_REG_PLUG_CTRL_FSM_ATTACHED_SRC;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, val);

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, &val);
	val |= S2MU106_REG_PLUG_CTRL_FSM_MANUAL_EN;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, val);
}

static unsigned s2mu106_get_status(void *_data, u64 flag)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	u64 one = 1;

	if (pdic_data->status_reg & (one << flag)) {
		pdic_data->status_reg &= ~(one << flag); /* clear the flag */
		return 1;
	} else {
		return 0;
	}
}

static bool s2mu106_poll_status(void *_data)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	struct i2c_client *i2c = pdic_data->i2c;
	struct device *dev = &i2c->dev;
	u8 intr[S2MU106_MAX_NUM_INT_STATUS] = {0};
	int ret = 0, retry = 0;
	u64 status_reg_val = 0;
	msg_header_type header;
	int data_obj_num = 0;
	u64 one = 1;

	ret = s2mu106_usbpd_bulk_read(i2c, S2MU106_REG_INT_STATUS0,
			S2MU106_MAX_NUM_INT_STATUS, intr);

	dev_info(dev, "%s status[0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
			__func__, intr[0], intr[1], intr[2], intr[3], intr[4], intr[5], intr[6]);

	if ((intr[0] | intr[1] | intr[2] | intr[3] | intr[4] | intr[5]) == 0)
		goto out;

	if ((intr[2] & S2MU106_REG_INT_STATUS2_WAKEUP) ||
		(intr[4] & S2MU106_REG_INT_STATUS4_CC12_DET_IRQ))
		s2mu106_set_irq_enable(pdic_data, ENABLED_INT_0, ENABLED_INT_1,
				ENABLED_INT_2, ENABLED_INT_3, ENABLED_INT_4, ENABLED_INT_5);

	/* when occur detach & attach atomic */
	if (intr[4] & S2MU106_REG_INT_STATUS4_USB_DETACH) {
		status_reg_val |= one <<PLUG_DETACH;
	}

	mutex_lock(&pdic_data->lpm_mutex);
	if ((intr[4] & S2MU106_REG_INT_STATUS4_PLUG_IRQ) &&
			!pdic_data->lpm_mode && !pdic_data->is_water_detect)
		status_reg_val |= one << PLUG_ATTACH;
	else if (pdic_data->lpm_mode &&
				(intr[4] & S2MU106_REG_INT_STATUS4_PLUG_IRQ) &&
									!pdic_data->is_water_detect)
		retry = 1;
	mutex_unlock(&pdic_data->lpm_mutex);

	if (retry) {
		msleep(40);
		mutex_lock(&pdic_data->lpm_mutex);
		if ((intr[4] & S2MU106_REG_INT_STATUS4_PLUG_IRQ) &&
				!pdic_data->lpm_mode && !pdic_data->is_water_detect)
			status_reg_val |= one << PLUG_ATTACH;
		mutex_unlock(&pdic_data->lpm_mutex);
	}

	if (intr[5] & S2MU106_REG_INT_STATUS5_HARD_RESET)
		status_reg_val |= one << MSG_HARDRESET;

	if (intr[0] & S2MU106_REG_INT_STATUS0_MSG_GOODCRC)
		status_reg_val |= one << MSG_GOODCRC;

	if (intr[1] & S2MU106_REG_INT_STATUS1_MSG_PR_SWAP)
		status_reg_val |= one << MSG_PR_SWAP;

	if (intr[2] & S2MU106_REG_INT_STATUS2_MSG_SOFTRESET)
		status_reg_val |= one << MSG_SOFTRESET;

	if (intr[1] & S2MU106_REG_INT_STATUS1_MSG_DR_SWAP)
		status_reg_val |= one << MSG_DR_SWAP;

	if (intr[0] & S2MU106_REG_INT_STATUS0_MSG_ACCEPT)
		status_reg_val |= one << MSG_ACCEPT;

	/* function that support dp control */
	if (intr[4] & S2MU106_REG_INT_STATUS4_MSG_PASS) {
		if (intr[3] & S2MU106_REG_INT_STATUS3_UNS_CMD_DATA)
			status_reg_val |= one << MSG_RID;
		else {
			usbpd_protocol_rx(data);
			if (data->msg_received == 0)
				goto out;

			header = data->protocol_rx.msg_header;
			data_obj_num = header.num_data_objs;

			if (data_obj_num > 0)
				s2mu106_usbpd_check_msg(data, &status_reg_val);

			if (intr[1] & S2MU106_REG_INT_STATUS1_MSG_PSRDY)
				status_reg_val |= one << MSG_PSRDY;

			if (intr[2] & S2MU106_REG_INT_STATUS2_MSG_REQUEST)
				status_reg_val |= one << MSG_REQUEST;

			if (intr[1] & S2MU106_REG_INT_STATUS1_MSG_REJECT)
				status_reg_val |= one << MSG_REJECT;

			if (intr[2] & S2MU106_REG_INT_STATUS2_MSG_WAIT)
				status_reg_val |= one << MSG_WAIT;

			if (intr[4] & S2MU106_REG_INT_STATUS4_MSG_ERROR)
				status_reg_val |= one << MSG_ERROR;

			if (intr[1] & S2MU106_REG_INT_STATUS1_MSG_PING)
				status_reg_val |= one << MSG_PING;

			if (intr[1] & S2MU106_REG_INT_STATUS1_MSG_GETSNKCAP)
				status_reg_val |= one << MSG_GET_SNK_CAP;

			if (intr[1] & S2MU106_REG_INT_STATUS1_MSG_GETSRCCAP)
				status_reg_val |= one << MSG_GET_SRC_CAP;

			if (intr[2] & S2MU106_REG_INT_STATUS2_MSG_VCONN_SWAP)
				status_reg_val |= one << MSG_VCONN_SWAP;
		}
	}
out:
	pdic_data->status_reg |= status_reg_val;

	/* complete wait msg */
	if (pdic_data->status_reg & data->wait_for_msg_arrived) {
		data->wait_for_msg_arrived = 0;
		complete(&data->msg_arrived);
	}

	return 0;
}

static void s2mu106_soft_reset(void *_data)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	struct i2c_client *i2c = pdic_data->i2c;

	s2mu106_self_soft_reset(i2c);
}

static int s2mu106_hard_reset(void *_data)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	struct i2c_client *i2c = pdic_data->i2c;
	int ret;
	u8 reg;
	u8 Read_Value = 0;

	if (pdic_data->rid != REG_RID_UNDF && pdic_data->rid != REG_RID_MAX)
		return 0;

	reg = S2MU106_REG_MSG_SEND_CON;

	ret = s2mu106_usbpd_write_reg(i2c, reg, S2MU106_REG_MSG_SEND_CON_SOP_HardRST
			| S2MU106_REG_MSG_SEND_CON_OP_MODE);
	if (ret < 0)
		goto fail;

	ret = s2mu106_usbpd_write_reg(i2c, reg, S2MU106_REG_MSG_SEND_CON_SOP_HardRST
			| S2MU106_REG_MSG_SEND_CON_OP_MODE
			| S2MU106_REG_MSG_SEND_CON_SEND_MSG_EN);
	if (ret < 0)
		goto fail;

    /* USB PD CC Off*/
	Read_Value &= ~S2MU106_REG_PLUG_CTRL_CC_MANUAL_MASK;
	Read_Value |= S2MU106_REG_PLUG_CTRL_CC_MANUAL_EN;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, Read_Value);

	ret = s2mu106_usbpd_write_reg(i2c, reg, S2MU106_REG_MSG_SEND_CON_OP_MODE
										| S2MU106_REG_MSG_SEND_CON_HARD_EN);
	udelay(1);
	ret = s2mu106_usbpd_write_reg(i2c, reg, S2MU106_REG_MSG_SEND_CON_HARD_EN);
	if (ret < 0)
		goto fail;

	s2mu106_self_soft_reset(i2c);

	pdic_data->status_reg = 0;

	return 0;

fail:
	return -EIO;
}

static int s2mu106_receive_message(void *data)
{
	struct s2mu106_usbpd_data *pdic_data = data;
	struct i2c_client *i2c = pdic_data->i2c;
	struct device *dev = &i2c->dev;
	int obj_num = 0;
	int ret = 0;

	ret = s2mu106_read_msg_header(i2c, &pdic_data->header);
	if (ret < 0)
		dev_err(dev, "%s read msg header error\n", __func__);

	if (pdic_data->header.spec_revision < 2 &&
			pdic_data->header.num_data_objs > 0 &&
			pdic_data->header.msg_type == 6) {
		usleep_range(500, 900);
		ret = s2mu106_read_msg_header(i2c, &pdic_data->header);
		if (ret < 0)
			dev_err(dev, "%s read msg header error\n", __func__);
	}

	obj_num = pdic_data->header.num_data_objs;

	if (obj_num > 0) {
		ret = s2mu106_read_msg_obj(i2c,
			obj_num, &pdic_data->obj[0]);
	}

	return ret;
}

static int s2mu106_tx_msg(void *_data,
		msg_header_type *header, data_obj_type *obj)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	struct i2c_client *i2c = pdic_data->i2c;
	int ret = 0;
	int i = 0;
	int count = 0;
	u8 send_msg[30];

	pr_info("%s, \n", __func__);

	/* if there is no attach, skip tx msg */
	if (pdic_data->detach_valid)
		goto done;

#if 0
	/* using msg id counter at s2mu106 */
	s2mu106_usbpd_read_reg(pdic_data->i2c, S2MU106_REG_ID_MONITOR, &reg_data);
	msg_id = reg_data & S2MU106_REG_ID_MONITOR_MSG_ID_MASK;
	header->msg_id = msg_id;
#endif
	send_msg[0] = header->byte[0];
	send_msg[1] = header->byte[1];

	count = header->num_data_objs;

	for (i = 0; i < count; i++) {
		send_msg[2 + (i * 4)] = obj[i].byte[0];
		send_msg[3 + (i * 4)] = obj[i].byte[1];
		send_msg[4 + (i * 4)] = obj[i].byte[2];
		send_msg[5 + (i * 4)] = obj[i].byte[3];
	}

	ret = s2mu106_write_msg_all(i2c, count, send_msg);
	if (ret < 0)
		goto done;

	s2mu106_send_msg(i2c);

done:
	return ret;
}

static int s2mu106_rx_msg(void *_data,
		msg_header_type *header, data_obj_type *obj)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	int i;
	int count = 0;

	if (!s2mu106_receive_message(pdic_data)) {
		header->word = pdic_data->header.word;
		count = pdic_data->header.num_data_objs;
		if (count > 0) {
			for (i = 0; i < count; i++)
				obj[i].object = pdic_data->obj[i].object;
		}
		pdic_data->header.word = 0; /* To clear for duplicated call */
		return 0;
	} else {
		return -EINVAL;
	}
}

static int s2mu106_set_otg_control(void *_data, int val)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;

	mutex_lock(&pdic_data->cc_mutex);
	if (val)
		vbus_turn_on_ctrl(pdic_data, VBUS_ON);
	else
		vbus_turn_on_ctrl(pdic_data, VBUS_OFF);
	mutex_unlock(&pdic_data->cc_mutex);

	return 0;
}

static int s2mu106_set_cc_control(void *_data, int val)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	int ret = 0;

	mutex_lock(&pdic_data->cc_mutex);
	ret = s2mu106_usbpd_set_cc_control(pdic_data, val);
	mutex_unlock(&pdic_data->cc_mutex);

	return ret;
}

static int s2mu106_vbus_on_check(void *_data)
{
#if defined(CONFIG_PM_S2MU106)
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;

	return s2mu106_usbpd_check_vbus(pdic_data, 3500, VBUS_ON);
#else
	return 1;
#endif
}

static void s2mu106_set_pwr_opmode(void *_data, int mode)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;

	typec_set_pwr_opmode(pdic_data->port, mode);
}

#if defined(CONFIG_CHECK_CTYPE_SIDE) || defined(CONFIG_CCIC_SYSFS)
static int s2mu106_get_side_check(void *_data)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	struct i2c_client *i2c = pdic_data->i2c;
	u8 val, cc1_val, cc2_val;

	s2mu106_usbpd_test_read(pdic_data);

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_MON1, &val);

	cc1_val = val & S2MU106_REG_CTRL_MON_CC1_MASK;
	cc2_val = (val & S2MU106_REG_CTRL_MON_CC2_MASK) >> S2MU106_REG_CTRL_MON_CC2_SHIFT;

	if (cc1_val == USBPD_Rd)
		return USBPD_UP_SIDE;
	else if (cc2_val == USBPD_Rd)
		return USBPD_DOWN_SIDE;
	else
		return USBPD_UNDEFFINED_SIDE;
}
#endif
static int s2mu106_set_vconn_source(void *_data, int val)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	struct i2c_client *i2c = pdic_data->i2c;
	u8 reg_data = 0, reg_val = 0, cc1_val = 0, cc2_val = 0;

	if (!pdic_data->vconn_en) {
		pr_err("%s, not support vconn source\n", __func__);
		return -1;
	}

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_MON1, &reg_val);
	cc1_val = (reg_val & S2MU106_REG_CTRL_MON_CC1_MASK) >> S2MU106_REG_CTRL_MON_CC1_SHIFT;
	cc2_val = (reg_val & S2MU106_REG_CTRL_MON_CC2_MASK) >> S2MU106_REG_CTRL_MON_CC2_SHIFT;

	if (val == USBPD_VCONN_ON) {
		if (cc1_val == USBPD_Rd) {
			if (cc2_val == USBPD_Ra) {
				s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, &reg_data);
				reg_data &= ~S2MU106_REG_PLUG_CTRL_RpRd_VCONN_MASK;
				reg_data |= (S2MU106_REG_PLUG_CTRL_RpRd_CC2_VCONN |
						S2MU106_REG_PLUG_CTRL_VCONN_MANUAL_EN);
				s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, reg_data);
			}
		}
		if (cc2_val == USBPD_Rd) {
			if (cc1_val == USBPD_Ra) {
				s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, &reg_data);
				reg_data &= ~S2MU106_REG_PLUG_CTRL_RpRd_VCONN_MASK;
				reg_data |= (S2MU106_REG_PLUG_CTRL_RpRd_CC1_VCONN |
						S2MU106_REG_PLUG_CTRL_VCONN_MANUAL_EN);
				s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, reg_data);
			}
		}
	} else if (val == USBPD_VCONN_OFF) {
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, &reg_data);
				reg_data &= ~S2MU106_REG_PLUG_CTRL_RpRd_VCONN_MASK;
		reg_data |= S2MU106_REG_PLUG_CTRL_VCONN_MANUAL_EN;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, reg_data);
	} else
		return(-1);

	pdic_data->vconn_source = val;
	return 0;
}

static void s2mu106_usbpd_set_vconn_manual(struct s2mu106_usbpd_data *pdic_data, bool enable)
{
	u8 reg_data = 0;

	s2mu106_usbpd_read_reg(pdic_data->i2c, S2MU106_REG_PLUG_CTRL_RpRd, &reg_data);
	reg_data &= ~S2MU106_REG_PLUG_CTRL_RpRd_VCONN_MASK;

	if (enable)
		reg_data |= S2MU106_REG_PLUG_CTRL_VCONN_MANUAL_EN;

	s2mu106_usbpd_write_reg(pdic_data->i2c, S2MU106_REG_PLUG_CTRL_RpRd, reg_data);
}

static int s2mu106_get_vconn_source(void *_data, int *val)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;

	/* TODO
		set s2mu106 pdic register control */

	if (pdic_data->vconn_source != *val) {
		dev_info(pdic_data->dev, "%s, vconn_source(%d) != gpio val(%d)\n",
				__func__, pdic_data->vconn_source, *val);
		pdic_data->vconn_source = *val;
	}

	return 0;
}

/* val : sink(0) or source(1) */
static int s2mu106_set_power_role(void *_data, int val)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;

	pr_info("%s, power_role(%d)\n", __func__, val);

	if (val == USBPD_SINK) {
		pdic_data->is_pr_swap = true;
		s2mu106_assert_rd(data);
		s2mu106_snk(pdic_data->i2c);
	} else if (val == USBPD_SOURCE) {
		pdic_data->is_pr_swap = true;
		s2mu106_assert_rp(data);
		s2mu106_src(pdic_data->i2c);
	} else if (val == USBPD_DRP) {
		pdic_data->is_pr_swap = false;
		s2mu106_assert_drp(data);
		return 0;
	} else
		return(-1);

	pdic_data->power_role = val;
	return 0;
}

static int s2mu106_get_power_role(void *_data, int *val)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;

	*val = pdic_data->power_role;

	return 0;
}

static int s2mu106_set_data_role(void *_data, int val)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	struct i2c_client *i2c = pdic_data->i2c;
	u8 val_port, data_role;

	/* DATA_ROLE (0x18[2])
	 * 0 : UFP
	 * 1 : DFP
	 */
	if (val == USBPD_UFP) {
		data_role = S2MU106_REG_MSG_DATA_ROLE_UFP;
		s2mu106_ufp(i2c);
	} else {/* (val == USBPD_DFP) */
		data_role = S2MU106_REG_MSG_DATA_ROLE_DFP;
		s2mu106_dfp(i2c);
	}

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_MSG, &val_port);
	val_port = (val_port & ~S2MU106_REG_MSG_DATA_ROLE_MASK) | data_role;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_MSG, val_port);

	pdic_data->data_role = val;

#if defined(CONFIG_IFCONN_NOTIFIER)
	process_dr_swap(pdic_data);
#endif
	return 0;
}

static int s2mu106_get_data_role(void *_data, int *val)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;
	*val = pdic_data->data_role;
	return 0;
}

static int s2mu106_set_check_msg_pass(void *_data, int val)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	struct s2mu106_usbpd_data *pdic_data = data->phy_driver_data;

	dev_info(pdic_data->dev, "%s: check_msg_pass val(%d)\n", __func__, val);

	pdic_data->check_msg_pass = val;

	return 0;
}
#ifndef CONFIG_SEC_FACTORY
static void s2mu106_usbpd_set_rp_scr_sel(struct s2mu106_usbpd_data *pdic_data,
							CCIC_RP_SCR_SEL scr_sel)
{
	struct i2c_client *i2c = pdic_data->i2c;
	u8 data = 0;
	pr_info("%s: scr_sel : (%d)\n", __func__, scr_sel);
	switch (scr_sel) {
	case PLUG_CTRL_RP0:
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, &data);
		data &= ~S2MU106_REG_PLUG_CTRL_RP_SEL_MASK;
		data |= S2MU106_REG_PLUG_CTRL_RP0;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, data);
		break;
	case PLUG_CTRL_RP80:
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, &data);
		data &= ~S2MU106_REG_PLUG_CTRL_RP_SEL_MASK;
		data |= S2MU106_REG_PLUG_CTRL_RP80;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, data);
		break;
	case PLUG_CTRL_RP180:
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, &data);
		data &= ~S2MU106_REG_PLUG_CTRL_RP_SEL_MASK;
		data |= S2MU106_REG_PLUG_CTRL_RP180;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, data);
		break;
	default:
		break;
	}
	return;
}
#endif

int s2mu106_usbpd_check_msg(void *_data, u64 *val)
{
	struct usbpd_data *data = (struct usbpd_data *) _data;
	int data_type = 0;
	int msg_type = 0;
	int vdm_type = 0;
	int vdm_command = 0;
	u64 shift = 0;
	u64 one = 1;

	dev_info(data->dev, "%s\n", __func__);

	if (data->protocol_rx.msg_header.num_data_objs == 0)
		data_type = USBPD_CTRL_MSG;
	else
		data_type = USBPD_DATA_MSG;

	msg_type = data->protocol_rx.msg_header.msg_type;

	if (data_type == USBPD_CTRL_MSG)
		return 0;

	if (data_type == USBPD_DATA_MSG) {
		switch (msg_type) {
		case USBPD_Source_Capabilities:
			shift = MSG_SRC_CAP;
			*val |= one << shift;
			break;
		case USBPD_Request:
			shift = MSG_REQUEST;
			*val |= one << shift;
			break;
		case USBPD_Sink_Capabilities:
			shift = MSG_SNK_CAP;
			*val |= one << shift;
			break;
		case USBPD_BIST:
			shift = MSG_BIST;
			*val |= one << shift;
			break;
		case USBPD_Vendor_Defined:
			vdm_command = data->protocol_rx.data_obj[0].structured_vdm.command;
			vdm_type = data->protocol_rx.data_obj[0].structured_vdm.vdm_type;

			if (vdm_type == Unstructured_VDM) {
				dev_info(data->dev, "%s : uvdm msg received!\n", __func__);
				shift = UVDM_MSG;
				*val |= one << shift;
				break;
			}

			switch (vdm_command) {
			case DisplayPort_Status_Update:
				shift = VDM_DP_STATUS_UPDATE;
				*val |= one << shift;
				break;
			case DisplayPort_Configure:
				shift = VDM_DP_CONFIGURE;
				*val |= one << shift;
				break;
			case Attention:
				shift = VDM_ATTENTION;
				*val |= one << shift;
				break;
			case Exit_Mode:
				shift = VDM_EXIT_MODE;
				*val |= one << shift;
				break;
			case Enter_Mode:
				shift = VDM_ENTER_MODE;
				*val |= one << shift;
				break;
			case Discover_Modes:
				shift = VDM_ENTER_MODE;
				*val |= one << shift;
				break;
			case Discover_SVIDs:
				shift = VDM_DISCOVER_SVID;
				*val |= one << shift;
				break;
			case Discover_Identity:
				shift = VDM_DISCOVER_IDENTITY;
				*val |= one << shift;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}

	dev_info(data->dev, "%s: msg status(%lld)\n", __func__, *val);

	return 0;
}

static int s2mu106_usbpd_set_cc_control(struct s2mu106_usbpd_data  *pdic_data, int val)
{
	struct i2c_client *i2c = pdic_data->i2c;
	u8 data = 0;

	dev_info(pdic_data->dev, "%s, (%d)\n", __func__, val);

	if (pdic_data->detach_valid) {
		dev_info(pdic_data->dev, "%s, ignore cc control\n", __func__);
		return 0;
	}

	if (val) {
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, &data);
		data &= ~S2MU106_REG_PLUG_CTRL_CC_MANUAL_MASK;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, data);

		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL, &data);
		data |= S2MU106_REG_PLUG_CTRL_ECO_SRC_CAP_RDY;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL, data);
	} else {
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, &data);
		data &= ~S2MU106_REG_PLUG_CTRL_CC_MANUAL_MASK;
		data |= S2MU106_REG_PLUG_CTRL_CC_MANUAL_EN;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC12, data);

		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL, &data);
		data &= ~S2MU106_REG_PLUG_CTRL_ECO_SRC_CAP_RDY;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL, data);
	}

	return 0;
}

static void s2mu106_dfp(struct i2c_client *i2c)
{
	u8 data;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_MSG, &data);
	data |= S2MU106_REG_MSG_DATA_ROLE_MASK;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_MSG, data);
}

static void s2mu106_ufp(struct i2c_client *i2c)
{
	u8 data;
	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_MSG, &data);
	data &= ~S2MU106_REG_MSG_DATA_ROLE_MASK;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_MSG, data);
}

static void s2mu106_src(struct i2c_client *i2c)
{
	u8 data;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_MSG, &data);
	data = (data & ~S2MU106_REG_MSG_POWER_ROLE_MASK) | S2MU106_REG_MSG_POWER_ROLE_SOURCE;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_MSG, data);
}

static void s2mu106_snk(struct i2c_client *i2c)
{
	u8 data;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_MSG, &data);
	data = (data & ~S2MU106_REG_MSG_POWER_ROLE_MASK) | S2MU106_REG_MSG_POWER_ROLE_SINK;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_MSG, data);
}

#if defined(CONFIG_IFCONN_NOTIFIER)
static void s2mu106_notify_pdic_rid(struct s2mu106_usbpd_data *pdic_data, int rid)
{
	pdic_data->is_factory_mode = false;
	if (rid == RID_523K)
		pdic_data->is_factory_mode = true;
	/* rid */
	ifconn_event_work(pdic_data, IFCONN_NOTIFY_MUIC,
				IFCONN_NOTIFY_ID_RID, rid, NULL);

	if (rid == REG_RID_523K || rid == REG_RID_619K || rid == REG_RID_OPEN) {
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_USB, IFCONN_NOTIFY_ID_USB,
					IFCONN_NOTIFY_EVENT_ATTACH, NULL);
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_USB,
					IFCONN_NOTIFY_ID_USB, IFCONN_NOTIFY_EVENT_DETACH, NULL);
		pdic_data->is_host = HOST_OFF;
		pdic_data->is_client = CLIENT_OFF;
	} else if (rid == REG_RID_301K) {
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_USB, IFCONN_NOTIFY_ID_USB,
					IFCONN_NOTIFY_EVENT_USB_ATTACH_UFP, NULL);
		pdic_data->is_host = HOST_OFF;
		pdic_data->is_client = CLIENT_ON;
	}

	dev_info(pdic_data->dev, "%s : attached rid state(%d)", __func__, rid);
}
#endif

static void s2mu106_usbpd_check_rid(struct s2mu106_usbpd_data *pdic_data)
{
	struct i2c_client *i2c = pdic_data->i2c;
	u8 rid;
	int prev_rid = pdic_data->rid;

	msleep(200);

	if (pdic_data->check_rid_wa)
		rid = REG_RID_619K;
	else {
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_ADC_STATUS, &rid);
		rid = (rid & S2MU106_PDIC_RID_MASK) >> S2MU106_PDIC_RID_SHIFT;
	}
	dev_info(pdic_data->dev, "%s : attached rid state(%d)", __func__, rid);

	if (rid) {
		if (pdic_data->rid != rid) {
			pdic_data->rid = rid;
			if (prev_rid >= REG_RID_OPEN && rid >= REG_RID_OPEN)
				dev_err(pdic_data->dev,
				  "%s : rid is not changed, skip notify(%d)", __func__, rid);
			else
				s2mu106_notify_pdic_rid(pdic_data, rid);
		}

		if (rid >= REG_RID_MAX) {
			dev_err(pdic_data->dev, "%s : overflow rid value", __func__);
			return;
		}
	}
}

int s2mu106_set_normal_mode(struct s2mu106_usbpd_data *pdic_data)
{
	u8 data;
	u8 data_lpm;
	int ret = 0;
	struct i2c_client *i2c = pdic_data->i2c;
	struct device *dev = &i2c->dev;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, &data);
	data &= ~(S2MU106_REG_PLUG_CTRL_MODE_MASK | S2MU106_REG_PLUG_CTRL_RP_SEL_MASK);
	data |= S2MU106_REG_PLUG_CTRL_DRP | S2MU106_REG_PLUG_CTRL_RP80;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PD_CTRL, &data_lpm);
	data_lpm &= ~S2MU106_REG_LPM_EN;

	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, data);
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PD_CTRL, data_lpm);

	pdic_data->lpm_mode = false;

	s2mu106_set_irq_enable(pdic_data, ENABLED_INT_0, ENABLED_INT_1,
				ENABLED_INT_2, ENABLED_INT_3, ENABLED_INT_4, ENABLED_INT_5);

	dev_info(dev, "%s s2mu106 exit lpm mode\n", __func__);

	return ret;
}

int s2mu106_usbpd_lpm_check(struct s2mu106_usbpd_data *pdic_data)
{
	u8 data_lpm = 0;
	struct i2c_client *i2c = pdic_data->i2c;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PD_CTRL, &data_lpm);

	return (data_lpm & S2MU106_REG_LPM_EN);
}

void s2mu106_usbpd_set_mode(struct s2mu106_usbpd_data *pdic_data,
													CCIC_LPM_MODE_SEL mode)
{
	u8 data_lpm = 0;
	struct i2c_client *i2c = pdic_data->i2c;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PD_CTRL, &data_lpm);
	if (mode == PD_LPM_MODE)
		data_lpm |= S2MU106_REG_LPM_EN;
	else if (mode == PD_NORMAL_MODE)
		data_lpm &= ~S2MU106_REG_LPM_EN;
	else {
		pr_info("%s mode val(%d) is invalid\n", __func__, mode);
		return;
	}

	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PD_CTRL, data_lpm);
}

void s2mu106_usbpd_set_vbus_wakeup(struct s2mu106_usbpd_data *pdic_data,
													CCIC_VBUS_WAKEUP_SEL sel)
{
	struct i2c_client *i2c = pdic_data->i2c;
	u8 data = 0;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PD_TRIM, &data);
	if (sel == VBUS_WAKEUP_ENABLE)
		data &= ~S2MU106_REG_VBUS_WAKEUP_DIS;
	else if (sel == VBUS_WAKEUP_DISABLE)
		data |= S2MU106_REG_VBUS_WAKEUP_DIS;
	else {
		pr_info("%s sel val(%d) is invalid\n", __func__, sel);
		return;
	}

	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PD_TRIM, data);
}

int s2mu106_get_plug_monitor(struct s2mu106_usbpd_data *pdic_data, u8 *data)
{
	u8 reg_val;
	int ret = 0;
	struct i2c_client *i2c = pdic_data->i2c;

	if (&data[0] == NULL || &data[1] == NULL) {
		pr_err("%s NULL point data\n", __func__);
		return -1;
	}

	ret = s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_MON1, &reg_val);
	if (ret < 0) {
		pr_err("%s: S2MU106_REG_PLUG_MON1 Read err : %d\n",	__func__, ret);
		return ret;
	}

	data[0] = reg_val & S2MU106_REG_CTRL_MON_CC1_MASK;
	data[1] = (reg_val & S2MU106_REG_CTRL_MON_CC2_MASK) >> S2MU106_REG_CTRL_MON_CC2_SHIFT;
	pr_info("%s, water cc mon cc1 : 0x%X, cc2 : 0x%X\n", __func__, data[0], data[1]);

	return ret;
}

int s2mu106_set_lpm_mode(struct s2mu106_usbpd_data *pdic_data)
{
	u8 data, data_lpm;
	int ret = 0;
	struct i2c_client *i2c = pdic_data->i2c;
	struct device *dev = &i2c->dev;
	u8 intr[S2MU106_MAX_NUM_INT_STATUS] = {0};

	pdic_data->lpm_mode = true;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, &data);
	data &= ~(S2MU106_REG_PLUG_CTRL_MODE_MASK | S2MU106_REG_PLUG_CTRL_RP_SEL_MASK);
	data |= S2MU106_REG_PLUG_CTRL_DFP | S2MU106_REG_PLUG_CTRL_RP0;
	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PD_CTRL, &data_lpm);
	data_lpm |= S2MU106_REG_LPM_EN;

	s2mu106_set_irq_enable(pdic_data, 0, 0, 0, 0, 0, 0);

	ret = s2mu106_usbpd_bulk_read(i2c, S2MU106_REG_INT_STATUS0,
			S2MU106_MAX_NUM_INT_STATUS, intr);

	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, data);
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PD_CTRL, data_lpm);

	if (pdic_data->detach_valid == false) {
		s2mu106_usbpd_detach_init(pdic_data);
		s2mu106_usbpd_notify_detach(pdic_data);
	}

	dev_info(dev, "%s s2mu106 enter lpm mode\n", __func__);

	return ret;
}

void s2mu106_set_water_detect_pre_cond(struct s2mu106_usbpd_data *pdic_data)
{
	int i;
	u8 cc_val[2] = {0,};

	s2mu106_set_normal_mode(pdic_data);
	mdelay(10);

	for (i = 0; i < 14; i++) {
		if (s2mu106_get_plug_monitor(pdic_data, cc_val) < 0) {
			pr_info("%s abnormal", __func__);
			mdelay(10);
		} else {
			if (IS_CC_RP(cc_val[0], cc_val[1]))
				break;
			else {
				pr_info("%s Not Rp yet. ", __func__);
				mdelay(10);
			}
		}
	}
}

void s2mu106_set_water_1st_detect(struct s2mu106_usbpd_data *pdic_data)
{
	u8 data, data_lpm;
	struct i2c_client *i2c = pdic_data->i2c;
	struct device *dev = &i2c->dev;

	pdic_data->lpm_mode = true;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_RP, S2MU106_THRESHOLD_MAX);

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, &data);
	data &= ~(S2MU106_REG_PLUG_CTRL_MODE_MASK | S2MU106_REG_PLUG_CTRL_RP_SEL_MASK);
	data |= S2MU106_REG_PLUG_CTRL_DFP | S2MU106_REG_PLUG_CTRL_RP80
			| S2MU106_REG_PLUG_CTRL_DETECT_BAT_DISABLE_MASK
			| S2MU106_REG_PLUG_CTRL_DETECT_OCP_DISABLE_MASK;
	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PD_CTRL, &data_lpm);
	data_lpm |= S2MU106_REG_LPM_EN;

	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, data);
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PD_CTRL, data_lpm);

	s2mu106_set_irq_enable(pdic_data, 0, 0, 0, 0, 0, 0);

	usleep_range(500, 900);

	data &= ~(S2MU106_REG_PLUG_CTRL_MODE_MASK | S2MU106_REG_PLUG_CTRL_RP_SEL_MASK);
	data |= S2MU106_REG_PLUG_CTRL_DFP | S2MU106_REG_PLUG_CTRL_RP0;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, data);

	dev_info(dev, "%s s2mu106 enter water chk lpm mode\n", __func__);
}

static bool s2mu106_is_water_detected_2nd_seq(struct s2mu106_usbpd_data *pdic_data, u8 *cc_val)
{
	struct i2c_client *i2c = pdic_data->i2c;
	u8 cc_chk[2] = {0,};

	if (cc_val[0] == USBPD_Rp)
		cc_chk[0] = 1;
	if (cc_val[1] == USBPD_Rp)
		cc_chk[1] = 1;

	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_RD,
							S2MU106_THRESHOLD_214MV);
	s2mu106_set_lpm_mode(pdic_data);
	s2mu106_usbpd_update_bit(i2c, S2MU106_REG_PD_CTRL,
						S2MU106_REG_LPM_EN, 0, 0);
	msleep(300);

	if (s2mu106_get_plug_monitor(pdic_data, cc_val) < 0) {
		pr_err("%s Failed to get the plug monitor.\n", __func__);
		return false;
	}

	/* Rd is detected due to the water CAPACITOR. */
	if (((cc_chk[0] && !cc_chk[1]) && (cc_val[0] == USBPD_Rd)) ||
		((cc_chk[1] && !cc_chk[0]) && (cc_val[1] == USBPD_Rd)) ||
		((cc_chk[0] && cc_chk[1]) && ((cc_val[0] == USBPD_Rd) && (cc_val[1] == USBPD_Rd)))) {
			return true;
	}

	return false;
}

static void _S2MU106_PDIC_enter_to_water(struct s2mu106_usbpd_data *pdic_data)
{
	struct i2c_client *i2c = pdic_data->i2c;
#if defined(CONFIG_USB_HW_PARAM) && !defined(CONFIG_USB_TYPEC_MANAGER_NOTIFIER)
	struct otg_notify *o_notify = get_otg_notify();
#endif

#if defined(CONFIG_CCIC_NOTIFIER)
	ccic_event_work(pdic_data,
		CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_WATER, CCIC_NOTIFY_ATTACH, 0);
#endif
	pdic_data->is_water_detect = true;
	pdic_data->water_detect_cnt = 0;
	s2mu106_set_lpm_mode(pdic_data);
	s2mu106_usbpd_update_bit(i2c, S2MU106_REG_PD_CTRL,
									S2MU106_REG_LPM_EN, 0, 0);
#if defined(CONFIG_USB_HW_PARAM) && !defined(CONFIG_USB_TYPEC_MANAGER_NOTIFIER)
	if (o_notify)
		inc_hw_param(o_notify, USB_CCIC_WATER_INT_COUNT);
#endif
}

static void S2MU106_PDIC_water_detect_handler(struct work_struct *work)
{
	struct s2mu106_usbpd_data *pdic_data =
		container_of(work, struct s2mu106_usbpd_data, water_detect_handler.work);
	struct i2c_client *i2c = pdic_data->i2c;

	u8 cc_val[2] = {0,};

	pr_info("%s enter", __func__);
	mutex_lock(&pdic_data->_mutex);

	/*
	 * Cancel the detect handler,
	 * in case the muic notifies cable attach or dry signal,
	 * or the water chk cnt is over,
	 * or ccic already detected the water.
	 */
	if (!pdic_data->is_muic_water_detect
		|| pdic_data->water_detect_cnt > WATER_CHK_RETRY_CNT
		|| pdic_data->is_water_detect) {
		pr_info("%s: detect handler is canceled", __func__);
		goto WATER_OUT;
	}

	s2mu106_set_water_detect_pre_cond(pdic_data);
	s2mu106_set_water_1st_detect(pdic_data);
	msleep(400);

	if (s2mu106_get_plug_monitor(pdic_data, cc_val) < 0) {
		pr_err("%s Failed to get the plug monitor.\n", __func__);
		goto WATER_MODE_OUT;
	}

	if (IS_CC_WATER(cc_val[0], cc_val[1]))	{
		pr_info("%s, water is detected, cc1 : 0x%X, cc2 : 0x%X\n",
				__func__, cc_val[0], cc_val[1]);
		_S2MU106_PDIC_enter_to_water(pdic_data);
		goto WATER_MODE_OUT;
	} else {
		pr_info("%s, 1st chk is not water, cc1 : 0x%X, cc2 : 0x%X\n",
								__func__, cc_val[0], cc_val[1]);
		if (s2mu106_is_water_detected_2nd_seq(pdic_data, cc_val)) {
			pr_info("%s, 2nd seq, water is detected, cc1 : 0x%X, cc2 : 0x%X\n",
					__func__, cc_val[0], cc_val[1]);
			_S2MU106_PDIC_enter_to_water(pdic_data);
			goto WATER_MODE_OUT;
		}

		pr_info("%s, 2nd chk : not water, cc1 : 0x%X, cc2 : 0x%X\n",
								__func__, cc_val[0], cc_val[1]);

		if (pdic_data->water_detect_cnt++ >= WATER_CHK_RETRY_CNT) {
			pdic_data->is_water_detect = false;
			pdic_data->water_detect_cnt = 0;
#if defined(CONFIG_CCIC_NOTIFIER)
			ccic_event_work(pdic_data,
				CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_WATER, CCIC_NOTIFY_DETACH, 0);
#endif
		} else {
			/*
			 * Retry the cc check,
			 * in case of invalid status.
			 * (200ms interval + 175ms check duration) * 5 times
			 */
			cancel_delayed_work(&pdic_data->water_detect_handler);
			schedule_delayed_work(&pdic_data->water_detect_handler,
							msecs_to_jiffies(S2MU106_WATER_CHK_INTERVAL_TIME));
		}
	}
WATER_MODE_OUT:
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_RD,
								S2MU106_THRESHOLD_428MV);
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_RP,
								S2MU106_THRESHOLD_2057MV);
WATER_OUT:
	mutex_unlock(&pdic_data->_mutex);
	return;
}

static void S2MU106_PDIC_water_dry_handler(struct work_struct *work)
{
	struct s2mu106_usbpd_data *pdic_data =
		container_of(work, struct s2mu106_usbpd_data, water_dry_handler.work);
	struct i2c_client *i2c = pdic_data->i2c;
	u8 cc_val[2] = {0,};

	pr_info("%s enter", __func__);
	mutex_lock(&pdic_data->_mutex);

	s2mu106_set_water_detect_pre_cond(pdic_data);
	s2mu106_set_water_1st_detect(pdic_data);
	msleep(400);

	if (s2mu106_get_plug_monitor(pdic_data, cc_val) < 0) {
		pr_err("%s Failed to get the plug monitor.\n", __func__);
	}

	if (IS_CC_RP(cc_val[0], cc_val[1]))	{
		pr_info("%s, 1st, water DRY is detected, cc1 : 0x%X, cc2 : 0x%X\n",
				__func__, cc_val[0], cc_val[1]);

		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_RD,
									S2MU106_THRESHOLD_428MV);
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_RP,
									S2MU106_THRESHOLD_2057MV);
#if defined(CONFIG_CCIC_NOTIFIER)
		ccic_event_work(pdic_data,
			CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_WATER, CCIC_NOTIFY_DETACH, 0);
#endif
	} else {
		pr_info("%s, It is not DRIED yet, cc1 : 0x%X, cc2 : 0x%X\n",
								__func__, cc_val[0], cc_val[1]);

#if defined(CONFIG_CCIC_NOTIFIER)
		ccic_event_work(pdic_data,
			CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_WATER, CCIC_NOTIFY_ATTACH, 0);
#endif
		s2mu106_set_lpm_mode(pdic_data);
		s2mu106_usbpd_update_bit(i2c, S2MU106_REG_PD_CTRL,
										S2MU106_REG_LPM_EN, 0, 0);
	}

	mutex_unlock(&pdic_data->_mutex);
	return;
}

static void s2mu106_usbpd_otg_attach(struct s2mu106_usbpd_data *pdic_data)
{
	struct i2c_client *i2c = pdic_data->i2c;
	struct device *dev = &i2c->dev;

	/* otg */
	pdic_data->is_host = HOST_ON;
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
	pdic_data->power_role_dual = DUAL_ROLE_PROP_PR_SRC;
#elif defined(CONFIG_TYPEC)
	pdic_data->typec_power_role = TYPEC_SOURCE;
	typec_set_pwr_role(pdic_data->port, pdic_data->typec_power_role);
#endif
#if defined(CONFIG_IFCONN_NOTIFIER)
	/* USB */
	ifconn_event_work(pdic_data, IFCONN_NOTIFY_USB, IFCONN_NOTIFY_ID_USB,
								IFCONN_NOTIFY_EVENT_USB_ATTACH_DFP, NULL);
#endif
#ifdef CONFIG_PM_S2MU106
		s2mu106_usbpd_check_vbus(pdic_data, 80, VBUS_OFF);
#endif

#if defined(CONFIG_MACH_RP400)
	pr_info("Skip VBUS turn ON!!!\n");
#else
	/* add to turn on external 5V */
	vbus_turn_on_ctrl(pdic_data, VBUS_ON);
#endif

	usbpd_manager_acc_handler_cancel(dev);
}

#if defined(CONFIG_MUIC_NOTIFIER)
static int type3_handle_notification(struct notifier_block *nb,
		unsigned long action, void *data)
{
#if defined(CONFIG_CCIC_NOTIFIER)
	CC_NOTI_ATTACH_TYPEDEF *p_noti = (CC_NOTI_ATTACH_TYPEDEF *)data;
	muic_attached_dev_t attached_dev = p_noti->cable_type;
#else
	muic_attached_dev_t attached_dev = *(muic_attached_dev_t *)data;
#endif
	struct s2mu106_usbpd_data *pdic_data =
		container_of(nb, struct s2mu106_usbpd_data,
			     type3_nb);
	struct i2c_client *i2c = pdic_data->i2c;
	u8 reg_data = 0;

#if (defined(CONFIG_USB_HW_PARAM) && !defined(CONFIG_USB_TYPEC_MANAGER_NOTIFIER)) || \
	(!defined(CONFIG_SEC_FACTORY) && defined(CONFIG_USB_HOST_NOTIFY))
	struct otg_notify *o_notify = get_otg_notify();
#endif
	mutex_lock(&pdic_data->lpm_mutex);
	pr_info("%s action:%d, attached_dev:%d, lpm:%d, pdic_data->is_otg_vboost:%d, pdic_data->is_otg_reboost:%d\n",
		__func__, (int)action, (int)attached_dev, pdic_data->lpm_mode,
		(int)pdic_data->is_otg_vboost, (int)pdic_data->is_otg_reboost);

	if ((action == MUIC_PDIC_NOTIFY_CMD_ATTACH) &&
		(attached_dev == ATTACHED_DEV_TYPE3_MUIC)) {
		pdic_data->is_muic_water_detect = false;
		pdic_data->water_detect_cnt = 0;
		if (pdic_data->lpm_mode) {
			pr_info("%s try to exit lpm mode-->\n", __func__);
			s2mu106_set_normal_mode(pdic_data);
			pr_info("%s after exit lpm mode<--\n", __func__);
		}
	} else if ((action == MUIC_PDIC_NOTIFY_CMD_ATTACH) &&
		attached_dev == ATTACHED_DEV_CHK_WATER_REQ) {
		pr_info("%s, ATTACH : MUIC REQUESTED WATER CHECK\n", __func__);
		s2mu106_usbpd_set_vconn_manual(pdic_data, true);
		pdic_data->is_muic_water_detect = true;
		pdic_data->water_detect_cnt = 0;
		pdic_data->is_water_detect = false;
		cancel_delayed_work(&pdic_data->water_detect_handler);
		schedule_delayed_work(&pdic_data->water_detect_handler, msecs_to_jiffies(100));
	} else if ((action == MUIC_PDIC_NOTIFY_CMD_ATTACH) &&
		attached_dev == ATTACHED_DEV_CHK_WATER_DRY_REQ) {
		pr_info("%s, ATTACH : MUIC REQUESTED WATER DRY CHECK\n", __func__);
		cancel_delayed_work(&pdic_data->water_dry_handler);
		schedule_delayed_work(&pdic_data->water_dry_handler, msecs_to_jiffies(100));
	} else if ((action == MUIC_PDIC_NOTIFY_CMD_ATTACH) &&
		attached_dev == ATTACHED_DEV_OTG_MUIC) {
		s2mu106_usbpd_otg_attach(pdic_data);
	} else if ((action == MUIC_PDIC_NOTIFY_CMD_DETACH) &&
		attached_dev == ATTACHED_DEV_UNDEFINED_RANGE_MUIC) {
		pr_info("%s, DETACH : ATTACHED_DEV_UNDEFINED_RANGE_MUIC(Water DRY)\n", __func__);
		pdic_data->is_muic_water_detect = false;
		pdic_data->water_detect_cnt = 0;
		pdic_data->is_water_detect = false;
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PD_CTRL, &reg_data);
		reg_data |= S2MU106_REG_LPM_EN;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PD_CTRL, reg_data);
#if defined(CONFIG_USB_HW_PARAM) && !defined(CONFIG_USB_TYPEC_MANAGER_NOTIFIER)
		if (o_notify)
			inc_hw_param(o_notify, USB_CCIC_DRY_INT_COUNT);
#endif
#if defined(CONFIG_CCIC_NOTIFIER)
		ccic_event_work(pdic_data,
			CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_WATER, CCIC_NOTIFY_DETACH, 0);
#endif
	} else if (action == MUIC_PDIC_NOTIFY_CMD_DETACH) {
		if (!pdic_data->lpm_mode) {
			pr_info("%s try to enter lpm mode-->\n", __func__);
			s2mu106_set_lpm_mode(pdic_data);
			pr_info("%s after enter lpm mode<--\n", __func__);
		}
	}
#if !defined(CONFIG_SEC_FACTORY) && defined(CONFIG_USB_HOST_NOTIFY)
	else if ((action == MUIC_PDIC_NOTIFY_CMD_ATTACH)
			&& (attached_dev == ATTACHED_DEV_CHECK_OCP)
			&& pdic_data->is_otg_vboost
			&& pdic_data->data_role_dual ==
								IFCONN_NOTIFY_EVENT_USB_ATTACH_DFP) {
		if (o_notify) {
			if (is_blocked(o_notify, NOTIFY_BLOCK_TYPE_HOST)) {
				pr_info("%s, upsm mode, skip OCP handling\n", __func__);
				goto EOH;
			}
		}
		if (pdic_data->is_otg_reboost) {
			/* todo : over current event to platform */
			pr_info("%s, CHECK_OCP, Can't afford it(OVERCURRENT)\n", __func__);
			if (o_notify)
				send_otg_notify(o_notify, NOTIFY_EVENT_OVERCURRENT, 0);
			goto EOH;
		}
		ccic_event_work(pdic_data,
			CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH, 1/*attach*/, 1/*rprd*/);

		pr_info("%s, CHECK_OCP, start OCP W/A\n", __func__);
		pdic_data->is_otg_reboost = true;
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_CC_HOLD, &reg_data);
		reg_data |= S2MU106_REG_PLUG_CTRL_CC_HOLD_BIT;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC_HOLD, reg_data);

		s2mu106_usbpd_set_rp_scr_sel(pdic_data, PLUG_CTRL_RP80);
		vbus_turn_on_ctrl(pdic_data, VBUS_OFF);
		vbus_turn_on_ctrl(pdic_data, VBUS_ON);

		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_CC_HOLD, &reg_data);
		reg_data &= ~S2MU106_REG_PLUG_CTRL_CC_HOLD_BIT;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC_HOLD, reg_data);
	}
EOH:
#endif
	mutex_unlock(&pdic_data->lpm_mutex);

	return 0;
}
#endif

static void s2mu106_usbpd_prevent_watchdog_reset(
	struct s2mu106_usbpd_data *pdic_data)
{
	struct i2c_client *i2c = pdic_data->i2c;
	u8 val = 0;

	mutex_lock(&pdic_data->lpm_mutex);
	wake_lock(&pdic_data->wake_lock);
	if (!pdic_data->lpm_mode) {
		if (s2mu106_usbpd_lpm_check(pdic_data) == 0) {
			s2mu106_usbpd_read_reg(i2c, S2MU106_REG_INT_STATUS2, &val);
			s2mu106_usbpd_set_vbus_wakeup(pdic_data, VBUS_WAKEUP_DISABLE);
			pr_info("%s force to lpm mode\n", __func__);
			s2mu106_usbpd_set_mode(pdic_data, PD_LPM_MODE);
			/* enable wakeup to check prevent function */
			s2mu106_set_irq_enable(pdic_data, ENABLED_INT_0, ENABLED_INT_1,
						ENABLED_INT_2_WAKEUP, ENABLED_INT_3, ENABLED_INT_4,
															ENABLED_INT_5);
			s2mu106_usbpd_set_vbus_wakeup(pdic_data, VBUS_WAKEUP_ENABLE);
			usleep_range(1000, 1200);
			s2mu106_usbpd_read_reg(i2c, S2MU106_REG_INT_STATUS2, &val);
			if (val & S2MU106_REG_INT_STATUS2_WAKEUP)
				pr_info("%s auto wakeup success\n", __func__);
			else {
				msleep(22);
				s2mu106_usbpd_set_vbus_wakeup(pdic_data, VBUS_WAKEUP_DISABLE);
				usleep_range(1000, 1200);
				s2mu106_usbpd_set_vbus_wakeup(pdic_data, VBUS_WAKEUP_ENABLE);
				usleep_range(1000, 1200);
				s2mu106_usbpd_read_reg(i2c, S2MU106_REG_INT_STATUS2, &val);
				if (val & S2MU106_REG_INT_STATUS2_WAKEUP)
					pr_info("%s auto wakeup success\n", __func__);
				else
					s2mu106_usbpd_set_mode(pdic_data, PD_NORMAL_MODE);
			}

			s2mu106_set_irq_enable(pdic_data, ENABLED_INT_0, ENABLED_INT_1,
								ENABLED_INT_2, ENABLED_INT_3, ENABLED_INT_4,
																ENABLED_INT_5);
		}
	}
	wake_unlock(&pdic_data->wake_lock);
	mutex_unlock(&pdic_data->lpm_mutex);
}

static void s2mu106_usbpd_rp_current_check(struct s2mu106_usbpd_data *pdic_data)
{
	struct i2c_client *i2c = pdic_data->i2c;
	struct device *dev = pdic_data->dev;
	u8 val = 0;
	u8 cc1_val = 0, cc2_val = 0;
	u8 rp_currentlvl = 0;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_FSM_MON, &val);

	cc1_val = val & S2MU106_REG_CTRL_MON_CC1_MASK;
	cc2_val = (val & S2MU106_REG_CTRL_MON_CC2_MASK) >> S2MU106_REG_CTRL_MON_CC2_SHIFT;

	dev_info(dev, "%s, rp check : cc1_val(%x), cc2_val(%x)\n",
					__func__, cc1_val, cc2_val);

	if (cc1_val == USBPD_10k || cc2_val == USBPD_10k)
		rp_currentlvl = RP_CURRENT_LEVEL3;
	else if (cc1_val == USBPD_22k || cc2_val == USBPD_22k)
		rp_currentlvl = RP_CURRENT_LEVEL2;
	else if (cc1_val == USBPD_56k || cc2_val == USBPD_56k)
		rp_currentlvl = RP_CURRENT_LEVEL_DEFAULT;

	pd_noti.sink_status.rp_currentlvl = rp_currentlvl;
	pd_noti.event = IFCONN_NOTIFY_EVENT_RP_ATTACH;
	ifconn_event_work(pdic_data, IFCONN_NOTIFY_MANAGER,
			IFCONN_NOTIFY_ID_POWER_STATUS, IFCONN_NOTIFY_EVENT_ATTACH, &pd_noti);
}

static void s2mu106_usbpd_detach_init(struct s2mu106_usbpd_data *pdic_data)
{
	struct device *dev = pdic_data->dev;
	struct usbpd_data *pd_data = dev_get_drvdata(dev);
	int ret = 0;

	dev_info(dev, "%s\n", __func__);

	mutex_lock(&pdic_data->cc_mutex);
	s2mu106_usbpd_set_cc_control(pdic_data, USBPD_CC_OFF);
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
	if (pdic_data->power_role_dual == DUAL_ROLE_PROP_PR_SRC) {
		vbus_turn_on_ctrl(pdic_data, VBUS_OFF);
	}
#elif defined(CONFIG_TYPEC)
	if (pdic_data->typec_power_role == TYPEC_SOURCE) {
		vbus_turn_on_ctrl(pdic_data, VBUS_OFF);
	}
	pd_data->pd_support = 0;
#endif
	pdic_data->detach_valid = true;
	mutex_unlock(&pdic_data->cc_mutex);
	usbpd_manager_plug_detach(dev, 0);

	/* wait flushing policy engine work */
	usbpd_cancel_policy_work(dev);
	s2mu106_self_soft_reset(pdic_data->i2c);

	pdic_data->status_reg = 0;
	usbpd_reinit(dev);
	pdic_data->rid = REG_RID_MAX;
	pdic_data->check_rid_wa = false;
	pdic_data->is_factory_mode = false;
	pdic_data->is_pr_swap = false;
	if (pdic_data->regulator_en)
		ret = regulator_disable(pdic_data->regulator);
#ifdef CONFIG_BATTERY_SAMSUNG
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
	pd_noti.sink_status.current_pdo_num = 0;
	pd_noti.sink_status.selected_pdo_num = 0;
	pd_noti.sink_status.rp_currentlvl = RP_CURRENT_LEVEL_NONE;
#endif
#endif
	s2mu106_usbpd_reg_init(pdic_data);
	s2mu106_set_vconn_source(pd_data, USBPD_VCONN_OFF);
}

static void s2mu106_usbpd_notify_detach(struct s2mu106_usbpd_data *pdic_data)
{
	struct device *dev = pdic_data->dev;
#if defined(CONFIG_IFCONN_NOTIFIER)
	/* MUIC */
	ifconn_event_work(pdic_data, IFCONN_NOTIFY_MUIC, IFCONN_NOTIFY_ID_ATTACH,
								IFCONN_NOTIFY_EVENT_DETACH, NULL);

	ifconn_event_work(pdic_data, IFCONN_NOTIFY_MUIC, IFCONN_NOTIFY_ID_RID,
								IFCONN_NOTIFY_EVENT_DETACH, NULL);

	if (pdic_data->is_host > HOST_OFF || pdic_data->is_client > CLIENT_OFF) {
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
		if (pdic_data->is_host > HOST_OFF ||
			pdic_data->power_role_dual == DUAL_ROLE_PROP_PR_SRC) {
#else
		if (pdic_data->is_host > HOST_OFF) {
#endif
			vbus_turn_on_ctrl(pdic_data, VBUS_OFF);
		}
		usbpd_manager_acc_detach(dev);
		/* usb or otg */
		dev_info(dev, "%s %d: is_host = %d, is_client = %d\n", __func__,
				__LINE__, pdic_data->is_host, pdic_data->is_client);
		pdic_data->is_host = HOST_OFF;
		pdic_data->is_client = CLIENT_OFF;
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
		pdic_data->power_role_dual = DUAL_ROLE_PROP_PR_NONE;
#elif defined(CONFIG_TYPEC)
		if (pdic_data->partner) {
			pr_info("%s, %d\n", __func__, __LINE__);
			if (!IS_ERR(pdic_data->partner)) {
				typec_unregister_partner(pdic_data->partner);
				pdic_data->partner = NULL;
			}
		}
		pdic_data->typec_power_role = TYPEC_SINK;
		pdic_data->typec_data_role = TYPEC_DEVICE;
#endif
		/* USB */
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_USB, IFCONN_NOTIFY_ID_USB,
										IFCONN_NOTIFY_EVENT_DETACH, NULL);
	}
#else
	usbpd_manager_plug_detach(dev, 1);
#endif
}
#ifdef CONFIG_CCIC_TRY_SNK
static void s2mu106_usbpd_try_snk(struct s2mu106_usbpd_data *pdic_data)
{
	struct device *dev = pdic_data->dev;
	struct i2c_client *i2c = pdic_data->i2c;
	u8 data;
	u8 intr[S2MU106_MAX_NUM_INT_STATUS] = {0};

	dev_info(dev, "%s\n", __func__);

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, &data);
	data &= ~S2MU106_REG_PLUG_CTRL_MODE_MASK;
	data |= S2MU106_REG_PLUG_CTRL_UFP;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, data);

	msleep(250);

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_MON2, &data);
	if ((data & S2MU106_PR_MASK) != S2MU106_PDIC_SINK) {
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, &data);
		data &= ~S2MU106_REG_PLUG_CTRL_MODE_MASK;
		data |= S2MU106_REG_PLUG_CTRL_DFP;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, data);

		msleep(200);
	}

	s2mu106_usbpd_bulk_read(i2c, S2MU106_REG_INT_STATUS0,
						S2MU106_MAX_NUM_INT_STATUS, intr);
}
#endif
static void s2mu106_usbpd_check_host(struct s2mu106_usbpd_data *pdic_data,
							CCIC_HOST_REASON host)
{
	if (host == HOST_ON && pdic_data->is_host == HOST_ON) {
		dev_info(pdic_data->dev, "%s %d: turn off host\n", __func__, __LINE__);
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_MUIC,
				IFCONN_NOTIFY_ID_ATTACH, IFCONN_NOTIFY_EVENT_DETACH, NULL);
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
		pdic_data->power_role_dual = DUAL_ROLE_PROP_PR_NONE;
#elif defined(CONFIG_TYPEC)
		pdic_data->typec_power_role = TYPEC_SINK;
		typec_set_pwr_role(pdic_data->port, pdic_data->typec_power_role);
#endif
		/* add to turn off external 5V */
		vbus_turn_on_ctrl(pdic_data, VBUS_OFF);
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_USB, IFCONN_NOTIFY_ID_USB,
										IFCONN_NOTIFY_EVENT_DETACH, NULL);
		pdic_data->is_host = HOST_OFF;
		msleep(300);
	} else if (host == HOST_OFF && pdic_data->is_host == HOST_OFF) {
		/* muic */
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_MUIC,
				IFCONN_NOTIFY_ID_OTG, IFCONN_NOTIFY_EVENT_ATTACH, NULL);
		s2mu106_usbpd_otg_attach(pdic_data);
	}
}

static void s2mu106_usbpd_check_client(struct s2mu106_usbpd_data *pdic_data,
							CCIC_DEVICE_REASON client)
{
	if (client == CLIENT_ON && pdic_data->is_client == CLIENT_ON) {
		dev_info(pdic_data->dev, "%s %d: turn off client\n", __func__, __LINE__);
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_MUIC,
				IFCONN_NOTIFY_ID_ATTACH, IFCONN_NOTIFY_EVENT_DETACH, NULL);
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
		pdic_data->power_role_dual = DUAL_ROLE_PROP_PR_NONE;
#elif defined(CONFIG_TYPEC)
		pdic_data->typec_power_role = TYPEC_SINK;
		typec_set_pwr_role(pdic_data->port, pdic_data->typec_power_role);
#endif
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_USB,
				IFCONN_NOTIFY_ID_USB, IFCONN_NOTIFY_EVENT_DETACH, NULL);
		pdic_data->is_client = CLIENT_OFF;
	}
}

static int s2mu106_check_port_detect(struct s2mu106_usbpd_data *pdic_data)
{
	struct i2c_client *i2c = pdic_data->i2c;
	struct device *dev = &i2c->dev;
	struct usbpd_data *pd_data = dev_get_drvdata(dev);
	u8 data, val;
	u8 cc1_val = 0, cc2_val = 0;
	int ret = 0;

	ret = s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_MON2, &data);
	if (ret < 0)
		dev_err(dev, "%s, i2c read PLUG_MON2 error\n", __func__);

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_MON1, &val);

	cc1_val = val & S2MU106_REG_CTRL_MON_CC1_MASK;
	cc2_val = (val & S2MU106_REG_CTRL_MON_CC2_MASK) >> S2MU106_REG_CTRL_MON_CC2_SHIFT;

	pdic_data->cc1_val = cc1_val;
	pdic_data->cc2_val = cc2_val;

	dev_err(dev, "%s, attach cc pin check cc1_val(%x), cc2_val(%x)\n",
					__func__, cc1_val, cc2_val);

#ifdef CONFIG_CCIC_DP
	if (cc1_val == USBPD_Rd)
		gpio_direction_output(pdic_data->usb_sw_sel, 1);
	else if (cc2_val == USBPD_Rd)
		gpio_direction_output(pdic_data->usb_sw_sel, 0);
#endif
#ifdef CONFIG_CCIC_TRY_SNK
	if ((data & S2MU106_PR_MASK) == S2MU106_PDIC_SOURCE) {
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
		if (!pdic_data->try_state_change) {
			s2mu106_usbpd_try_snk(pdic_data);
			s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_MON2, &data);
		}
#else
		s2mu106_usbpd_try_snk(pdic_data);
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_MON2, &data);
#endif
	}
#endif

#if defined(CONFIG_MACH_RP400)
	if(get_epd_usb_role()) { //host
		data &= ~S2MU106_PR_MASK;
		data |= S2MU106_PDIC_SOURCE;
	}
#endif

	if ((data & S2MU106_PR_MASK) == S2MU106_PDIC_SINK) {
		dev_info(dev, "SINK\n");
		pdic_data->detach_valid = false;
		pdic_data->power_role = PDIC_SINK;
		pdic_data->data_role = USBPD_UFP;
		s2mu106_snk(i2c);
		s2mu106_ufp(i2c);
		s2mu106_usbpd_prevent_watchdog_reset(pdic_data);
		usbpd_policy_reset(pd_data, PLUG_EVENT);
#if defined(CONFIG_IFCONN_NOTIFIER)
		dev_info(&i2c->dev, "%s %d: is_host = %d, is_client = %d\n", __func__,
					__LINE__, pdic_data->is_host, pdic_data->is_client);
		if (pdic_data->regulator_en) {
			ret = regulator_enable(pdic_data->regulator);
			if (ret)
				dev_err(&i2c->dev, "Failed to enable vconn LDO: %d\n", ret);
		}

		s2mu106_usbpd_check_host(pdic_data, HOST_ON);
		/* muic */
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_MUIC,
				IFCONN_NOTIFY_ID_ATTACH, IFCONN_NOTIFY_EVENT_ATTACH, NULL);
		ifconn_event_work(pdic_data, IFCONN_NOTIFY_MUIC,
				IFCONN_NOTIFY_ID_TA, IFCONN_NOTIFY_EVENT_ATTACH, NULL);
		if (!(pdic_data->rid == REG_RID_523K || pdic_data->rid == REG_RID_619K)) {
			if (pdic_data->is_client == CLIENT_OFF && pdic_data->is_host == HOST_OFF) {
				/* usb */
				pdic_data->is_client = CLIENT_ON;
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
				pdic_data->power_role_dual = DUAL_ROLE_PROP_PR_SNK;
#elif defined(CONFIG_TYPEC)
				pdic_data->typec_power_role = TYPEC_SINK;
				typec_set_pwr_role(pdic_data->port, pdic_data->typec_power_role);
#endif
				ifconn_event_work(pdic_data,
						IFCONN_NOTIFY_USB, IFCONN_NOTIFY_ID_USB,
								IFCONN_NOTIFY_EVENT_USB_ATTACH_UFP, NULL);
			}
		}
#endif
		s2mu106_usbpd_rp_current_check(pdic_data);
	} else if ((data & S2MU106_PR_MASK) == S2MU106_PDIC_SOURCE) {
		dev_info(dev, "SOURCE\n");
#if !defined(CONFIG_MACH_RP400)
		ret = s2mu106_usbpd_check_accessory(pdic_data);
		if (ret < 0) {
			dev_info(&i2c->dev, "%s attach accessory\n", __func__);
			return -1;
		}
#endif
		pdic_data->detach_valid = false;
		pdic_data->power_role = PDIC_SOURCE;
		pdic_data->data_role = USBPD_DFP;
		s2mu106_dfp(i2c);
		s2mu106_src(i2c);
		usbpd_policy_reset(pd_data, PLUG_EVENT);
#if defined(CONFIG_IFCONN_NOTIFIER)
		dev_info(&i2c->dev, "%s %d: is_host = %d, is_client = %d\n", __func__,
					__LINE__, pdic_data->is_host, pdic_data->is_client);
		s2mu106_usbpd_check_client(pdic_data, CLIENT_ON);
		s2mu106_usbpd_check_host(pdic_data, HOST_OFF);
#endif
		if (pdic_data->regulator_en) {
			ret = regulator_enable(pdic_data->regulator);
			if (ret)
				dev_err(&i2c->dev, "Failed to enable vconn LDO: %d\n", ret);
		}

		msleep(100);
	} else {
		dev_err(dev, "%s, PLUG Error\n", __func__);
#ifdef CONFIG_CCIC_TRY_SNK
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, &data);
		data &= ~(S2MU106_REG_PLUG_CTRL_MODE_MASK | S2MU106_REG_PLUG_CTRL_RP_SEL_MASK);
		data |= S2MU106_REG_PLUG_CTRL_DRP | S2MU106_REG_PLUG_CTRL_RP80;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_PORT, data);
#endif
		return -1;
	}

	s2mu106_set_irq_enable(pdic_data, ENABLED_INT_0, ENABLED_INT_1,
				ENABLED_INT_2, ENABLED_INT_3, ENABLED_INT_4, ENABLED_INT_5);

	return ret;
}

#if defined(CONFIG_SEC_FACTORY)
static int s2mu106_usbpd_check_619k(struct s2mu106_usbpd_data *pdic_data)
{
	u8 rid = 0;

	if (pdic_data->rid != REG_RID_619K)
		return false;

	msleep(250);
	s2mu106_usbpd_read_reg(pdic_data->i2c, S2MU106_REG_ADC_STATUS, &rid);
	rid = (rid & S2MU106_PDIC_RID_MASK) >> S2MU106_PDIC_RID_SHIFT;
	dev_info(pdic_data->dev, "%s %d: Detached, check if still 619K? => 0x%X\n",
					__func__, __LINE__, rid);

	if (rid == REG_RID_619K)
		return true;
	return false;
}
#endif

irqreturn_t s2mu106_irq_thread(int irq, void *data)
{
	struct s2mu106_usbpd_data *pdic_data = data;
	struct i2c_client *i2c = pdic_data->i2c;
	struct device *dev = &i2c->dev;
	struct usbpd_data *pd_data = dev_get_drvdata(dev);
	int ret = 0;
	unsigned attach_status = 0, rid_status = 0;

	dev_info(dev, "%s %d\n", __func__, irq);

	mutex_lock(&pd_data->accept_mutex);
	mutex_unlock(&pd_data->accept_mutex);

	mutex_lock(&pdic_data->_mutex);

	s2mu106_poll_status(pd_data);

#if defined(CONFIG_MACH_RP400)
	if(get_rp400_usb_lock()) {
		pr_err("%s USB event locked\n", __func__);
		goto out;
	}
#endif

#ifndef CONFIG_SEC_FACTORY
	if (pdic_data->lpcharge_water)
		goto out;
#endif

	if (pdic_data->status_reg == 0)
		goto out;

	if (s2mu106_get_status(pd_data, MSG_SOFTRESET))
		usbpd_rx_soft_reset(pd_data);

	if (s2mu106_get_status(pd_data, PLUG_DETACH)) {
#if defined(CONFIG_SEC_FACTORY)
		ret = s2mu106_usbpd_check_619k(pdic_data);
		if (ret)
			goto skip_detach;
#endif /* CONFIG_SEC_FACTORY */
		s2mu106_usbpd_set_rp_scr_sel(pdic_data, PLUG_CTRL_RP80);
		attach_status = s2mu106_get_status(pd_data, PLUG_ATTACH);
		rid_status = s2mu106_get_status(pd_data, MSG_RID);
		s2mu106_usbpd_detach_init(pdic_data);
		s2mu106_usbpd_notify_detach(pdic_data);
		if (attach_status) {
			ret = s2mu106_check_port_detect(pdic_data);
			if (ret >= 0) {
				if (rid_status) {
					pdic_data->check_rid_wa = true;
					s2mu106_usbpd_check_rid(pdic_data);
				}
				goto hard_reset;
			}
		}
#if 0
		mutex_lock(&pdic_data->lpm_mutex);
		if (!pdic_data->lpm_mode) {
			if (s2mu106_usbpd_lpm_check(pdic_data) > 0) {
				pr_info("%s force to normal mode\n", __func__);
				s2mu106_usbpd_set_mode(pdic_data, PD_NORMAL_MODE);
			}
			s2mu106_set_irq_enable(pdic_data, ENABLED_INT_0, ENABLED_INT_1,
				ENABLED_INT_2, ENABLED_INT_3, ENABLED_INT_4, ENABLED_INT_5);
		}
		mutex_unlock(&pdic_data->lpm_mutex);
#endif
		goto out;
	}

	if (s2mu106_get_status(pd_data, MSG_HARDRESET)) {
		mutex_lock(&pdic_data->cc_mutex);
		s2mu106_usbpd_set_cc_control(pdic_data, USBPD_CC_OFF);
		mutex_unlock(&pdic_data->cc_mutex);
		s2mu106_self_soft_reset(i2c);
		pdic_data->status_reg = 0;
		if (pdic_data->power_role == PDIC_SOURCE)
			s2mu106_dfp(i2c);
		else
			s2mu106_ufp(i2c);
		usbpd_rx_hard_reset(dev);
		usbpd_kick_policy_work(dev);
		goto out;
	}

#if defined(CONFIG_SEC_FACTORY)
skip_detach:
#endif /* CONFIG_SEC_FACTORY */
	if (s2mu106_get_status(pd_data, PLUG_ATTACH) && !pdic_data->is_pr_swap) {
		if (s2mu106_check_port_detect(data) < 0)
			goto out;
	}

	if (s2mu106_get_status(pd_data, MSG_RID)) {
		pdic_data->check_rid_wa = true;
		s2mu106_usbpd_check_rid(pdic_data);
	}

hard_reset:
	mutex_lock(&pdic_data->lpm_mutex);
	if (!pdic_data->lpm_mode)
		usbpd_kick_policy_work(dev);
	mutex_unlock(&pdic_data->lpm_mutex);
out:
	mutex_unlock(&pdic_data->_mutex);

	return IRQ_HANDLED;
}

static void s2mu106_usbpd_plug_work(struct work_struct *work)
{
	struct s2mu106_usbpd_data *pdic_data =
		container_of(work, struct s2mu106_usbpd_data, plug_work.work);

	s2mu106_irq_thread(-1, pdic_data);
}

static void s2mu106_usbpd_dr_work(struct work_struct *work)
{
	struct s2mu106_usbpd_data *usbpd_data =
		container_of(work, struct s2mu106_usbpd_data, dr_work.work);
	struct i2c_client *i2c = usbpd_data->i2c;

	dev_info(&i2c->dev, "%s : before - is_host : %d, is_client : %d\n",
		__func__, usbpd_data->is_host, usbpd_data->is_client);
	if (usbpd_data->is_host == HOST_ON) {
		ifconn_event_work(usbpd_data, IFCONN_NOTIFY_USB,
					IFCONN_NOTIFY_ID_USB, IFCONN_NOTIFY_EVENT_DETACH, NULL);

		ifconn_event_work(usbpd_data, IFCONN_NOTIFY_USB, IFCONN_NOTIFY_ID_USB,
								IFCONN_NOTIFY_EVENT_USB_ATTACH_UFP, NULL);
		usbpd_data->is_host = HOST_OFF;
		usbpd_data->is_client = CLIENT_ON;
	} else if (usbpd_data->is_client == CLIENT_ON) {
		ifconn_event_work(usbpd_data, IFCONN_NOTIFY_USB, IFCONN_NOTIFY_ID_USB,
									IFCONN_NOTIFY_EVENT_DETACH, NULL);

		ifconn_event_work(usbpd_data, IFCONN_NOTIFY_USB, IFCONN_NOTIFY_ID_USB,
							IFCONN_NOTIFY_EVENT_USB_ATTACH_DFP, NULL);
		usbpd_data->is_host = HOST_ON;
		usbpd_data->is_client = CLIENT_OFF;
	}
	dev_info(&i2c->dev, "%s : after - is_host : %d, is_client : %d\n",
		__func__, usbpd_data->is_host, usbpd_data->is_client);
}

static int s2mu106_usbpd_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_AUTHENTIC:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int s2mu106_usbpd_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct s2mu106_usbpd_data *pdic_data =
		power_supply_get_drvdata(psy);
	struct i2c_client *i2c = pdic_data->i2c;
	u8 data = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_AUTHENTIC:
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_VBUS_MUX, &data);
		data &= ~(S2MU106_REG_RD_OR_VBUS_MUX_SEL);
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_VBUS_MUX, data);
		break;
	case POWER_SUPPLY_PROP_USBPD_RESET:
		s2mu106_usbpd_set_vbus_wakeup(pdic_data, VBUS_WAKEUP_DISABLE);
		s2mu106_usbpd_set_vbus_wakeup(pdic_data, VBUS_WAKEUP_ENABLE);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int s2mu106_usbpd_psy_init(struct s2mu106_usbpd_data *_data, struct device *parent)
{
	struct power_supply_config psy_cfg = {};
	int ret = 0;

	if (_data == NULL || parent == NULL) {
		pr_err("%s NULL data\n", __func__);
		return -1;
	}

	_data->ccic_desc.name           = "s2mu106-usbpd";
	_data->ccic_desc.type           = POWER_SUPPLY_TYPE_UNKNOWN;
	_data->ccic_desc.get_property   = s2mu106_usbpd_get_property;
	_data->ccic_desc.set_property   = s2mu106_usbpd_set_property;
	_data->ccic_desc.properties     = ccic_props;
	_data->ccic_desc.num_properties = ARRAY_SIZE(ccic_props);

	psy_cfg.drv_data = _data;
	psy_cfg.supplied_to = ccic_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(ccic_supplied_to);

	_data->psy_ccic = power_supply_register(parent, &_data->ccic_desc, &psy_cfg);
	if (IS_ERR(_data->psy_ccic)) {
		ret = (int)PTR_ERR(_data->psy_ccic);
		pr_err("%s: Failed to Register psy_ccic, ret : %d\n", __func__, ret);
	}
	return ret;
}

static int s2mu106_usbpd_reg_init(struct s2mu106_usbpd_data *_data)
{
	struct i2c_client *i2c = _data->i2c;
	u8 data = 0;

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PHY_CTRL_IFG, &data);
	data |= S2MU106_PHY_IFG_35US << S2MU106_REG_IFG_SHIFT;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PHY_CTRL_IFG, data);

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_MSG_SEND_CON, &data);
	data |= S2MU106_REG_MSG_SEND_CON_HARD_EN;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_MSG_SEND_CON, data);

	/* for SMPL issue */
	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_ANALOG_OTP_0A, &data);
	data |= S2MU106_REG_OVP_ON;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_ANALOG_OTP_0A, data);

	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PD_CTRL_2, &data);
	data &= ~S2MU106_REG_CC_OCP_MASK;
	data |= S2MU106_CC_OCP_575MV << S2MU106_REG_CC_OCP_SHIFT;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PD_CTRL_2, data);

	/* enable Rd monitor status when cc is attached at sink */
	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_MON, &data);
	data |= S2MU106_REG_PLUG_CTRL_SET_MON_RD;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_MON, data);

	/* diable rd or vbus mux */
	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_VBUS_MUX, &data);
	data &= ~S2MU106_REG_RD_OR_VBUS_MUX_SEL;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_VBUS_MUX, data);

	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PHY_CTRL_00, 0x80);
	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_BMC_CTRL, &data);
	data |= 0x01 << 2;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_BMC_CTRL, data);

	/* set debounce time
	 devide by 300 and unit is ms */
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC_TIMER1, 0xe4);
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC_TIMER2, 0x0c);

	/* enable support acc */
	s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_CC_HOLD, &data);
	data |= S2MU106_REG_PLUG_CTRL_SUPPORT_ACC;
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_CC_HOLD, data);

	/* fix trim code */
	s2mu106_usbpd_read_reg(i2c, 0x00, &data);
	data |= 0x1 | 0x3 << 2;
	s2mu106_usbpd_write_reg(i2c, 0x00, data);

	data = 0;
	data |= (S2MU106_REG_PLUG_CTRL_SSM_DISABLE |
					S2MU106_REG_PLUG_CTRL_VDM_DISABLE);
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL, data);

	/* set Rd threshold to 400mV */
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_RD_2, S2MU106_THRESHOLD_600MV);
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_RP_2, S2MU106_THRESHOLD_1200MV);
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_RD, S2MU106_THRESHOLD_300MV | 0x40);
	s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_SET_RP, S2MU106_THRESHOLD_2057MV);

	if (_data->vconn_en) {
		/* Off Manual Rd setup & On Manual Vconn setup */
		s2mu106_usbpd_read_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, &data);
		data &= ~(S2MU106_REG_PLUG_CTRL_RpRd_MANUAL_EN_MASK);
		data |= S2MU106_REG_PLUG_CTRL_VCONN_MANUAL_EN;
		s2mu106_usbpd_write_reg(i2c, S2MU106_REG_PLUG_CTRL_RpRd, data);
	}
#ifdef CONFIG_PM_S2MU106
	s2mu106_usbpd_set_pmeter_mode(_data, PM_TYPE_VCHGIN);
#endif

	s2mu106_usbpd_set_vconn_manual(_data, true);

	return 0;
}

static irqreturn_t s2mu106_irq_isr(int irq, void *data)
{
	return IRQ_WAKE_THREAD;
}

static int s2mu106_usbpd_irq_init(struct s2mu106_usbpd_data *_data)
{
	struct i2c_client *i2c = _data->i2c;
	struct device *dev = &i2c->dev;
	int ret = 0;

	if (!_data->irq_gpio) {
		dev_err(dev, "%s No interrupt specified\n", __func__);
		return -ENXIO;
	}

/*	s2mu106_usbpd_bulk_read(i2c, S2MU106_REG_INT_STATUS0,
			S2MU106_MAX_NUM_INT_STATUS, intr);

	pr_info("%s status[0x%x 0x%x 0x%x 0x%x 0x%x]\n",
			__func__, intr[0], intr[1], intr[2], intr[3], intr[4]);
*/
	i2c->irq = gpio_to_irq(_data->irq_gpio);

	if (i2c->irq) {
		ret = request_threaded_irq(i2c->irq, s2mu106_irq_isr,
				s2mu106_irq_thread,
				(IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_NO_SUSPEND),
				"s2mu106-usbpd", _data);
		if (ret < 0) {
			dev_err(dev, "%s failed to request irq(%d)\n",
					__func__, i2c->irq);
			return ret;
		}

		ret = enable_irq_wake(i2c->irq);
		if (ret < 0)
			dev_err(dev, "%s failed to enable wakeup src\n",
					__func__);
	}

	if (_data->lpm_mode)
		s2mu106_set_irq_enable(_data, 0, 0, 0, 0, 0, 0);
	else
		s2mu106_set_irq_enable(_data, ENABLED_INT_0, ENABLED_INT_1,
				ENABLED_INT_2, ENABLED_INT_3, ENABLED_INT_4, ENABLED_INT_5);

	return ret;
}

static void s2mu106_usbpd_init_configure(struct s2mu106_usbpd_data *_data)
{
	s2mu106_usbpd_test_read(_data);
#if defined(CONFIG_PM_S2MU106)
	s2mu106_usbpd_get_pmeter_volt(_data);
	s2mu106_usbpd_get_pmeter_current(_data);
	pr_info("%s, chgin(%d), chgin_i(%d)\n", __func__,
							_data->pm_chgin, _data->pm_chgin_i);
#endif
	usbpd_charger_test_read(_data);
	s2mu106_set_normal_mode(_data);
	pr_info("%s, usbpd irq gpio value(%d)\n", __func__,
									gpio_get_value(_data->irq_gpio));
	msleep(25);
	_data->detach_valid = true;
	s2mu106_self_soft_reset(_data->i2c);
	s2mu106_set_lpm_mode(_data);
	_data->detach_valid = false;
	s2mu106_usbpd_set_cc_control(_data, USBPD_CC_OFF);
	_data->lpm_mode = true;
	msleep(150); /* for abnormal PD TA */
	_data->is_factory_mode = false;
	s2mu106_set_normal_mode(_data);
	_data->lpm_mode = false;
}

static void s2mu106_usbpd_pdic_data_init(struct s2mu106_usbpd_data *_data)
{
	_data->check_msg_pass = false;
	_data->vconn_source = USBPD_VCONN_OFF;
	_data->rid = REG_RID_MAX;
#if defined(CONFIG_MACH_RP400)
	_data->is_host = 1;
#else
	_data->is_host = 0;
#endif
	_data->is_client = 0;
	_data->data_role_dual = 0;
	_data->power_role_dual = 0;
	_data->is_water_detect = false;
	_data->is_muic_water_detect = false;
#if defined(CONFIG_MACH_RP400)
	_data->detach_valid = false;
#else
	_data->detach_valid = true;
#endif
	_data->is_otg_vboost = false;
	_data->is_otg_reboost = false;
	_data->is_pr_swap = false;
	_data->vbus_access = false;
#ifndef CONFIG_SEC_FACTORY
	_data->lpcharge_water = false;
#endif
	_data->check_rid_wa = false;
#if defined(CONFIG_TYPEC)
#if defined(CONFIG_MACH_RP400)
	_data->typec_power_role = TYPEC_SOURCE;
	_data->typec_data_role = TYPEC_HOST;
#else
	_data->typec_power_role = TYPEC_SINK;
	_data->typec_data_role = TYPEC_DEVICE;
#endif
#endif
}

static int of_s2mu106_dt(struct device *dev,
			struct s2mu106_usbpd_data *_data)
{
	struct device_node *np_usbpd = dev->of_node;
	int ret = 0;

	if (np_usbpd == NULL) {
		dev_err(dev, "%s np NULL\n", __func__);
		return -EINVAL;
	} else {
		_data->irq_gpio = of_get_named_gpio(np_usbpd,
							"usbpd,usbpd_int", 0);
		if (_data->irq_gpio < 0) {
			dev_err(dev, "error reading usbpd irq = %d\n",
						_data->irq_gpio);
			_data->irq_gpio = 0;
		}
		ret = gpio_get_value(_data->irq_gpio);
		pr_info("%s, usbpd irq gpio value(%d)\n", __func__, ret);
		if (of_find_property(np_usbpd, "vconn-en", NULL))
			_data->vconn_en = true;
		else
			_data->vconn_en = false;

		if (of_find_property(np_usbpd, "regulator-en", NULL))
			_data->regulator_en = true;
		else
			_data->regulator_en = false;
#ifdef CONFIG_CCIC_DP
		_data->usb_sw_sel = of_get_named_gpio(np_usbpd, "usb_sw_sel", 0);

		if (!gpio_is_valid(_data->usb_sw_sel))
			dev_err(dev, "failed to get gpio usb_sw_sel\n");
#endif
		}
	return ret;
}

static int s2mu106_usbpd_probe(struct i2c_client *i2c,
				const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(i2c->dev.parent);
	struct s2mu106_usbpd_data *pdic_data;
	struct device *dev = &i2c->dev;
	int ret = 0;

	dev_info(dev, "%s\n", __func__);
	test_i2c = i2c;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "%s: i2c functionality check error\n", __func__);
		ret = -EIO;
		goto err_return;
	}

	pdic_data = kzalloc(sizeof(struct s2mu106_usbpd_data), GFP_KERNEL);
	if (!pdic_data) {
		dev_err(dev, "%s: failed to allocate driver data\n", __func__);
		ret = -ENOMEM;
		goto err_return;
	}

	/* save platfom data for gpio control functions */
	pdic_data->dev = &i2c->dev;
	pdic_data->i2c = i2c;
	i2c_set_clientdata(i2c, pdic_data);

	ret = of_s2mu106_dt(&i2c->dev, pdic_data);
	if (ret < 0)
		dev_err(dev, "%s: not found dt!\n", __func__);

	mutex_init(&pdic_data->_mutex);
	mutex_init(&pdic_data->lpm_mutex);
	mutex_init(&pdic_data->cc_mutex);
	wake_lock_init(&pdic_data->wake_lock, WAKE_LOCK_SUSPEND, "pdic_wake");

#ifdef CONFIG_PM_S2MU106
	pdic_data->psy_pm = power_supply_get_by_name("s2mu106_pmeter");
	if (!pdic_data->psy_pm)
		pr_err("%s: Fail to get pmeter\n", __func__);
	s2mu106_usbpd_set_pmeter_mode(pdic_data, PM_TYPE_VCHGIN);
	s2mu106_usbpd_set_pmeter_mode(pdic_data, PM_TYPE_ICHGIN);
#endif

	s2mu106_usbpd_init_configure(pdic_data);
	s2mu106_usbpd_pdic_data_init(pdic_data);

	if (pdic_data->regulator_en) {
		pdic_data->regulator = devm_regulator_get(dev, "vconn");
		if (IS_ERR(pdic_data->regulator)) {
			dev_err(dev, "%s: not found regulator vconn\n", __func__);
			pdic_data->regulator_en = false;
		} else
			ret = regulator_disable(pdic_data->regulator);
	}

	ret = usbpd_init(dev, pdic_data);
	if (ret < 0) {
		dev_err(dev, "failed on usbpd_init\n");
		goto err_return;
	}

	usbpd_set_ops(dev, &s2mu106_ops);

	s2mu106_usbpd_reg_init(pdic_data);

	pdic_data->pdic_queue =
	    alloc_workqueue(dev_name(dev), WQ_MEM_RECLAIM, 1);
	if (!pdic_data->pdic_queue) {
		dev_err(dev,
			"%s: Fail to Create Workqueue\n", __func__);
		goto err_return;
	}

#if defined(CONFIG_IFCONN_NOTIFIER)
	/* Create a work queue for the ccic irq thread */
	pdic_data->ifconn_wq
		= create_singlethread_workqueue("ifconn_notify_event");
	if (!pdic_data->ifconn_wq) {
		pr_err("%s failed to create work queue for ccic notifier\n",
								   __func__);
		goto err_return;
	}
#endif
	if (pdic_data->rid == REG_RID_UNDF)
		pdic_data->rid = REG_RID_MAX;
#if defined(CONFIG_TYPEC)
	ret = typec_init(pdic_data);
	if (ret < 0) {
		pr_err("failed to init typec\n");
		goto err_return;
	}
#if defined(CONFIG_MACH_RP400)
	pdic_data->typec_cap.port_type_set(&pdic_data->typec_cap, TYPEC_PORT_DFP);
#endif
#endif

	ret = s2mu106_usbpd_irq_init(pdic_data);
	if (ret) {
		dev_err(dev, "%s: failed to init irq(%d)\n", __func__, ret);
		goto fail_init_irq;
	}
	INIT_DELAYED_WORK(&pdic_data->water_detect_handler, S2MU106_PDIC_water_detect_handler);
	INIT_DELAYED_WORK(&pdic_data->water_dry_handler, S2MU106_PDIC_water_dry_handler);
	INIT_DELAYED_WORK(&pdic_data->water_dry_handler, S2MU106_PDIC_water_dry_handler);
	INIT_DELAYED_WORK(&pdic_data->plug_work, s2mu106_usbpd_plug_work);
	INIT_DELAYED_WORK(&pdic_data->dr_work, s2mu106_usbpd_dr_work);

	if (pdic_data->detach_valid) {
		mutex_lock(&pdic_data->_mutex);
		s2mu106_check_port_detect(pdic_data);
		s2mu106_usbpd_check_rid(pdic_data);
		mutex_unlock(&pdic_data->_mutex);
	}

	s2mu106_irq_thread(-1, pdic_data);

#if defined(CONFIG_MUIC_NOTIFIER)
	muic_ccic_notifier_register(&pdic_data->type3_nb,
			       type3_handle_notification,
			       MUIC_NOTIFY_DEV_PDIC);
#endif
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
	ret = dual_role_init(pdic_data);
	if (ret < 0) {
		pr_err("unable to allocate dual role descriptor\n");
		goto fail_init_irq;
	}

#endif
	ret = s2mu106_usbpd_psy_init(pdic_data, &i2c->dev);
	if (ret < 0) {
		pr_err("faled to register the ccic psy.\n");
	}

	dev_info(dev, "%s s2mu106 usbpd driver uploaded!\n", __func__);

	return 0;

fail_init_irq:
	if (i2c->irq)
		free_irq(i2c->irq, pdic_data);
err_return:
	return ret;
}

#if defined CONFIG_PM
static int s2mu106_usbpd_suspend(struct device *dev)
{
	struct usbpd_data *_data = dev_get_drvdata(dev);
	struct s2mu106_usbpd_data *pdic_data = _data->phy_driver_data;

	if (device_may_wakeup(dev))
		enable_irq_wake(pdic_data->i2c->irq);

#ifndef CONFIG_SEC_FACTORY
	disable_irq(pdic_data->i2c->irq);
#endif
	return 0;
}

static int s2mu106_usbpd_resume(struct device *dev)
{
	struct usbpd_data *_data = dev_get_drvdata(dev);
	struct s2mu106_usbpd_data *pdic_data = _data->phy_driver_data;

	if (device_may_wakeup(dev))
		disable_irq_wake(pdic_data->i2c->irq);

#ifndef CONFIG_SEC_FACTORY
	enable_irq(pdic_data->i2c->irq);
#endif
	return 0;
}
#else
#define s2mu106_muic_suspend NULL
#define s2mu106_muic_resume NULL
#endif

static int s2mu106_usbpd_remove(struct i2c_client *i2c)
{
	struct s2mu106_usbpd_data *_data = i2c_get_clientdata(i2c);

	if (_data) {
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
		devm_dual_role_instance_unregister(_data->dev,
						_data->dual_role);
		devm_kfree(_data->dev, _data->desc);
#elif defined(CONFIG_TYPEC)
		typec_unregister_port(_data->port);
#endif
		disable_irq_wake(_data->i2c->irq);
		free_irq(_data->i2c->irq, _data);
		mutex_destroy(&_data->_mutex);
		i2c_set_clientdata(_data->i2c, NULL);
		kfree(_data);
	}
	return 0;
}

static const struct i2c_device_id s2mu106_usbpd_i2c_id[] = {
	{ USBPD_DEV_NAME, 1 },
	{}
};
MODULE_DEVICE_TABLE(i2c, s2mu106_i2c_id);

static struct of_device_id sec_usbpd_i2c_dt_ids[] = {
	{ .compatible = "sec-usbpd,i2c" },
	{ }
};

static void s2mu106_usbpd_shutdown(struct i2c_client *i2c)
{
	struct s2mu106_usbpd_data *_data = i2c_get_clientdata(i2c);

	if (!_data->i2c)
		return;
}

static usbpd_phy_ops_type s2mu106_ops = {
	.tx_msg			= s2mu106_tx_msg,
	.rx_msg			= s2mu106_rx_msg,
	.hard_reset		= s2mu106_hard_reset,
	.soft_reset		= s2mu106_soft_reset,
	.set_power_role		= s2mu106_set_power_role,
	.get_power_role		= s2mu106_get_power_role,
	.set_data_role		= s2mu106_set_data_role,
	.get_data_role		= s2mu106_get_data_role,
	.set_vconn_source	= s2mu106_set_vconn_source,
	.get_vconn_source	= s2mu106_get_vconn_source,
	.set_check_msg_pass	= s2mu106_set_check_msg_pass,
	.get_status		= s2mu106_get_status,
	.poll_status		= s2mu106_poll_status,
	.driver_reset		= s2mu106_driver_reset,
	.set_otg_control	= s2mu106_set_otg_control,
	.set_cc_control		= s2mu106_set_cc_control,
	.get_side_check		= s2mu106_get_side_check,
	.pr_swap			= s2mu106_pr_swap,
	.vbus_on_check		= s2mu106_vbus_on_check,
	.set_pwr_opmode		= s2mu106_set_pwr_opmode,
};

#if defined CONFIG_PM
const struct dev_pm_ops s2mu106_usbpd_pm = {
	.suspend = s2mu106_usbpd_suspend,
	.resume = s2mu106_usbpd_resume,
};
#endif

static struct i2c_driver s2mu106_usbpd_driver = {
	.driver		= {
		.name	= USBPD_DEV_NAME,
		.of_match_table	= sec_usbpd_i2c_dt_ids,
#if defined CONFIG_PM
		.pm	= &s2mu106_usbpd_pm,
#endif /* CONFIG_PM */
	},
	.probe		= s2mu106_usbpd_probe,
	.remove		= s2mu106_usbpd_remove,
	.shutdown	= s2mu106_usbpd_shutdown,
	.id_table	= s2mu106_usbpd_i2c_id,
};

static int __init s2mu106_usbpd_init(void)
{
	return i2c_add_driver(&s2mu106_usbpd_driver);
}
late_initcall(s2mu106_usbpd_init);

static void __exit s2mu106_usbpd_exit(void)
{
	i2c_del_driver(&s2mu106_usbpd_driver);
}
module_exit(s2mu106_usbpd_exit);

MODULE_DESCRIPTION("s2mu106 USB PD driver");
MODULE_LICENSE("GPL");
