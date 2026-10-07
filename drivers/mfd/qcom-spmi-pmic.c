// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2015, 2017-2020, The Linux Foundation.
 * All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spmi.h>
#include <linux/regmap.h>
#include <linux/of_platform.h>
#include <linux/qti-regmap-debugfs.h>
#include <linux/usb/typec_ucsi.h>

#define PMIC_REV2		0x101
#define PMIC_REV3		0x102
#define PMIC_REV4		0x103
#define PMIC_TYPE		0x104
#define PMIC_SUBTYPE		0x105

#define PMIC_TYPE_VALUE		0x51

#define COMMON_SUBTYPE		0x00
#define PM8941_SUBTYPE		0x01
#define PM8841_SUBTYPE		0x02
#define PM8019_SUBTYPE		0x03
#define PM8226_SUBTYPE		0x04
#define PM8110_SUBTYPE		0x05
#define PMA8084_SUBTYPE		0x06
#define PMI8962_SUBTYPE		0x07
#define PMD9635_SUBTYPE		0x08
#define PM8994_SUBTYPE		0x09
#define PMI8994_SUBTYPE		0x0a
#define PM8916_SUBTYPE		0x0b
#define PM8004_SUBTYPE		0x0c
#define PM8909_SUBTYPE		0x0d
#define PM8950_SUBTYPE		0x10
#define PMI8950_SUBTYPE		0x11
#define PM8998_SUBTYPE		0x14
#define PMI8998_SUBTYPE		0x15
#define PM8005_SUBTYPE		0x18

static const struct of_device_id pmic_spmi_id_table[] = {
	{ .compatible = "qcom,spmi-pmic", .data = (void *)COMMON_SUBTYPE },
	{ .compatible = "qcom,pm8941",    .data = (void *)PM8941_SUBTYPE },
	{ .compatible = "qcom,pm8841",    .data = (void *)PM8841_SUBTYPE },
	{ .compatible = "qcom,pm8019",    .data = (void *)PM8019_SUBTYPE },
	{ .compatible = "qcom,pm8226",    .data = (void *)PM8226_SUBTYPE },
	{ .compatible = "qcom,pm8110",    .data = (void *)PM8110_SUBTYPE },
	{ .compatible = "qcom,pma8084",   .data = (void *)PMA8084_SUBTYPE },
	{ .compatible = "qcom,pmi8962",   .data = (void *)PMI8962_SUBTYPE },
	{ .compatible = "qcom,pmd9635",   .data = (void *)PMD9635_SUBTYPE },
	{ .compatible = "qcom,pm8994",    .data = (void *)PM8994_SUBTYPE },
	{ .compatible = "qcom,pmi8994",   .data = (void *)PMI8994_SUBTYPE },
	{ .compatible = "qcom,pm8916",    .data = (void *)PM8916_SUBTYPE },
	{ .compatible = "qcom,pm8004",    .data = (void *)PM8004_SUBTYPE },
	{ .compatible = "qcom,pm8909",    .data = (void *)PM8909_SUBTYPE },
	{ .compatible = "qcom,pm8950",    .data = (void *)PM8950_SUBTYPE },
	{ .compatible = "qcom,pmi8950",   .data = (void *)PMI8950_SUBTYPE },
	{ .compatible = "qcom,pm8998",    .data = (void *)PM8998_SUBTYPE },
	{ .compatible = "qcom,pmi8998",   .data = (void *)PMI8998_SUBTYPE },
	{ .compatible = "qcom,pm8005",    .data = (void *)PM8005_SUBTYPE },
	{ }
};

static void pmic_spmi_show_revid(struct regmap *map, struct device *dev)
{
	unsigned int rev2, minor, major, type, subtype;
	const char *name = "unknown";
	int ret, i;

	ret = regmap_read(map, PMIC_TYPE, &type);
	if (ret < 0)
		return;

	if (type != PMIC_TYPE_VALUE)
		return;

	ret = regmap_read(map, PMIC_SUBTYPE, &subtype);
	if (ret < 0)
		return;

	for (i = 0; i < ARRAY_SIZE(pmic_spmi_id_table); i++) {
		if (subtype == (unsigned long)pmic_spmi_id_table[i].data)
			break;
	}

	if (i != ARRAY_SIZE(pmic_spmi_id_table))
		name = pmic_spmi_id_table[i].compatible;

	ret = regmap_read(map, PMIC_REV2, &rev2);
	if (ret < 0)
		return;

	ret = regmap_read(map, PMIC_REV3, &minor);
	if (ret < 0)
		return;

	ret = regmap_read(map, PMIC_REV4, &major);
	if (ret < 0)
		return;

	/*
	 * In early versions of PM8941 and PM8226, the major revision number
	 * started incrementing from 0 (eg 0 = v1.0, 1 = v2.0).
	 * Increment the major revision number here if the chip is an early
	 * version of PM8941 or PM8226.
	 */
	if ((subtype == PM8941_SUBTYPE || subtype == PM8226_SUBTYPE) &&
	    major < 0x02)
		major++;

	if (subtype == PM8110_SUBTYPE)
		minor = rev2;

	dev_dbg(dev, "%x: %s v%d.%d\n", subtype, name, major, minor);
}

static const struct regmap_config spmi_regmap_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xffff,
	.fast_io	= true,
};

static const struct regmap_config spmi_regmap_can_sleep_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xffff,
	.fast_io	= false,
};

#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#define PM8350B_USID			3
#define PM8350B_DCDC_CMD_OTG_REG	0x2740
#define PM8350B_DCDC_OTG_CFG_REG	0x2753
#define PM8350B_OTG_EN_BIT		BIT(0)
#define PM8350B_OTG_EN_SRC_CFG_BIT	BIT(1)

#define PM8350B_USB_USB_SUSPEND_REG	0x2954
#define PM8350B_USB_SUSPEND_BIT		BIT(0)

#define PM8350B_USBIN_RT_STS_REG	0x2910
#define PM8350B_USBIN_PLUGIN_BIT	BIT(4)

#define PM8350B_TYPEC_MISC_STATUS_REG	0x2b0b
#define PM8350B_CC_ATTACHED_BIT		BIT(0)

#define TYPEC_TYPE_C_STATE_MACHINE_STATUS_REG	0x2b09
#define TYPEC_ATTACH_DETACH_STATE_BIT		BIT(5)

#define PM8350B_TYPEC_MODE_CFG_REG	0x2b44
#define PM8350B_EN_TRY_SNK_BIT		BIT(4)
#define PM8350B_EN_SNK_ONLY_BIT		BIT(1)
#define PM8350B_TYPEC_TRY_MODE_MASK	GENMASK(4, 3)
#define PM8350B_TYPEC_POWER_ROLE_CMD_MASK GENMASK(2, 0)

#define PM8350B_TYPEC_VCONN_CONTROL_REG	0x2b46
#define PM8350B_VCONN_EN_SRC_BIT	BIT(0)
#define PM8350B_VCONN_EN_VAL_BIT	BIT(1)

struct pm8350b_data {
	struct device		*dev;
	struct regmap		*regmap;
	struct mutex		lock;
};

static struct regmap *pm8350b_spmi_regmap;
static struct pm8350b_data *pm8350b_global_data;

static bool pm8350b_user_suspended;

bool pm8350b_is_user_suspended(void)
{
	return pm8350b_user_suspended;
}
EXPORT_SYMBOL_GPL(pm8350b_is_user_suspended);

int pm8350b_set_usb_suspend(bool suspend)
{
	if (!pm8350b_spmi_regmap)
		return -ENODEV;

	pm8350b_user_suspended = suspend;

	regmap_write(pm8350b_spmi_regmap, PM8350B_USB_USB_SUSPEND_REG,
		     suspend ? PM8350B_USB_SUSPEND_BIT : 0x00);

	pr_info("pm8350b_set_usb_suspend: register 0x2954 = 0x%02x\n",
		suspend ? PM8350B_USB_SUSPEND_BIT : 0x00);

	return 0;
}
EXPORT_SYMBOL_GPL(pm8350b_set_usb_suspend);

int pm8350b_get_usb_suspend(void)
{
	unsigned int val = 0;
	int ret;

	if (!pm8350b_spmi_regmap)
		return -ENODEV;

	ret = regmap_read(pm8350b_spmi_regmap, PM8350B_USB_USB_SUSPEND_REG, &val);
	if (ret)
		return ret;

	return !!(val & PM8350B_USB_SUSPEND_BIT);
}
EXPORT_SYMBOL_GPL(pm8350b_get_usb_suspend);

int pm8350b_is_power_supplied(void)
{
	unsigned int val = 0;
	int ret;

	if (!pm8350b_spmi_regmap)
		return -ENODEV;

	/* Check real-time VBUS presence: 0x2910 BIT(4) (USBIN_PLUGIN) */
	ret = regmap_read(pm8350b_spmi_regmap, PM8350B_USBIN_RT_STS_REG, &val);
	if (!ret && (val & PM8350B_USBIN_PLUGIN_BIT))
		return 1;

	return 0;
}
EXPORT_SYMBOL_GPL(pm8350b_is_power_supplied);

static void pm8350b_unsuspend_work(struct work_struct *work)
{
	if (!pm8350b_spmi_regmap)
		return;

	/* Only unsuspend if power is still supplied and user hasn't explicitly suspended */
	if (!pm8350b_user_suspended && pm8350b_is_power_supplied() > 0) {
		regmap_write(pm8350b_spmi_regmap, PM8350B_USB_USB_SUSPEND_REG, 0x00);
		pr_info("pm8350b_unsuspend_work: unsuspend -> register 0x2954 = 0x00\n");
	}
}
static DECLARE_DELAYED_WORK(pm8350b_unsuspend_dwork, pm8350b_unsuspend_work);

void pm8350b_disable_otg(void)
{
	if (!pm8350b_spmi_regmap)
		return;

	/* Cancel any pending unsuspend work */
	cancel_delayed_work(&pm8350b_unsuspend_dwork);

	/* Cut DCDC OTG boost (0x2740 = 0x00) and autonomous boost (0x2753 bit 1 = 0) */
	regmap_write(pm8350b_spmi_regmap, PM8350B_DCDC_CMD_OTG_REG, 0x00);
	regmap_update_bits(pm8350b_spmi_regmap, PM8350B_DCDC_OTG_CFG_REG,
			   PM8350B_OTG_EN_SRC_CFG_BIT, 0);

	/* Cut VCONN output (0x2b46 = 0x01) */
	regmap_update_bits(pm8350b_spmi_regmap, PM8350B_TYPEC_VCONN_CONTROL_REG,
			   PM8350B_VCONN_EN_SRC_BIT, PM8350B_VCONN_EN_SRC_BIT);

	/* Lock Type-C in Sink-Only mode (0x2b44 = 0x02) to prevent HW from entering Source mode and enabling 5V */
	regmap_update_bits(pm8350b_spmi_regmap, PM8350B_TYPEC_MODE_CFG_REG,
			   PM8350B_TYPEC_TRY_MODE_MASK | PM8350B_TYPEC_POWER_ROLE_CMD_MASK,
			   PM8350B_EN_SNK_ONLY_BIT);

	/* Suspend USB charging (0x2954 = 0x01) on power cut */
	regmap_write(pm8350b_spmi_regmap, PM8350B_USB_USB_SUSPEND_REG, PM8350B_USB_SUSPEND_BIT);

	pr_info("pm8350b_disable_otg: OTG disabled (boost cut 0x00, autonomous boost disabled, vconn forced off 0x01, sink-only 0x02, suspended 0x01)\n");
}
EXPORT_SYMBOL_GPL(pm8350b_disable_otg);

void pm8350b_enable_otg(void)
{
	unsigned int val = 0;

	if (!pm8350b_spmi_regmap)
		return;

	/* Cut DCDC OTG boost (0x2740 = 0x00) and autonomous boost (0x2753 bit 1 = 0) */
	regmap_write(pm8350b_spmi_regmap, PM8350B_DCDC_CMD_OTG_REG, 0x00);
	regmap_update_bits(pm8350b_spmi_regmap, PM8350B_DCDC_OTG_CFG_REG,
			   PM8350B_OTG_EN_SRC_CFG_BIT, 0);

	/* Cut VCONN output (0x2b46 = 0x01) */
	regmap_update_bits(pm8350b_spmi_regmap, PM8350B_TYPEC_VCONN_CONTROL_REG,
			   PM8350B_VCONN_EN_SRC_BIT, PM8350B_VCONN_EN_SRC_BIT);

	/* Only update 0x2b44 if not already in Try.SNK mode (0x10) */
	regmap_read(pm8350b_spmi_regmap, PM8350B_TYPEC_MODE_CFG_REG, &val);
	if ((val & (PM8350B_TYPEC_TRY_MODE_MASK | PM8350B_TYPEC_POWER_ROLE_CMD_MASK)) != PM8350B_EN_TRY_SNK_BIT) {
		regmap_update_bits(pm8350b_spmi_regmap, PM8350B_TYPEC_MODE_CFG_REG,
				   PM8350B_TYPEC_TRY_MODE_MASK | PM8350B_TYPEC_POWER_ROLE_CMD_MASK,
				   PM8350B_EN_TRY_SNK_BIT);
		pr_info("pm8350b_enable_otg: OTG enabled (try_snk 0x10, boost cut 0x00, autonomous boost disabled, vconn forced off 0x01)\n");
	}

	if (!pm8350b_user_suspended)
		mod_delayed_work(system_wq, &pm8350b_unsuspend_dwork, msecs_to_jiffies(2000));
}
EXPORT_SYMBOL_GPL(pm8350b_enable_otg);

static void pm8350b_set_otg(struct pm8350b_data *data, bool enable)
{
	mutex_lock(&data->lock);
	if (enable) {
		pm8350b_enable_otg();
		regmap_write(data->regmap, PM8350B_DCDC_CMD_OTG_REG, PM8350B_OTG_EN_BIT);
	} else {
		pm8350b_disable_otg();
	}
	mutex_unlock(&data->lock);

	pr_info("pm8350b_set_otg: PM8350B OTG boost %s (physical SPMI)\n",
		 enable ? "enabled" : "disabled");
}

static ssize_t otg_enable_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct pm8350b_data *data = dev_get_drvdata(dev);
	unsigned int cmd_reg = 0, cfg_reg = 0;
	int ret;

	if (!data || !data->regmap)
		return -ENODEV;

	ret = regmap_read(data->regmap, PM8350B_DCDC_CMD_OTG_REG, &cmd_reg);
	if (ret)
		return ret;

	regmap_read(data->regmap, PM8350B_DCDC_OTG_CFG_REG, &cfg_reg);

	return scnprintf(buf, PAGE_SIZE, "cmd=0x%02x cfg=0x%02x (otg_en=%d auto_en=%d)\n",
			 cmd_reg, cfg_reg,
			 !!(cmd_reg & PM8350B_OTG_EN_BIT),
			 !!(cfg_reg & PM8350B_OTG_EN_SRC_CFG_BIT));
}

static ssize_t otg_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct pm8350b_data *data = dev_get_drvdata(dev);
	bool enable;

	if (!data || !data->regmap)
		return -ENODEV;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	pm8350b_set_otg(data, enable);

	return count;
}
static DEVICE_ATTR_RW(otg_enable);

static ssize_t usb_suspend_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int val = pm8350b_get_usb_suspend();

	if (val < 0)
		return val;

	return scnprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t usb_suspend_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	bool suspend;
	int ret;

	if (kstrtobool(buf, &suspend))
		return -EINVAL;

	ret = pm8350b_set_usb_suspend(suspend);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(usb_suspend);

static ssize_t typec_mode_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct pm8350b_data *data = dev_get_drvdata(dev);
	unsigned int val = 0;
	const char *mode = "unknown";
	int ret;

	if (!data || !data->regmap)
		return -ENODEV;

	ret = regmap_read(data->regmap, PM8350B_TYPEC_MODE_CFG_REG, &val);
	if (ret)
		return ret;

	if (val & PM8350B_EN_SNK_ONLY_BIT)
		mode = "sink_only";
	else if (val & PM8350B_EN_TRY_SNK_BIT)
		mode = "try_snk";
	else
		mode = "drp";

	return scnprintf(buf, PAGE_SIZE, "0x%02x (%s)\n", val, mode);
}

static ssize_t typec_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct pm8350b_data *data = dev_get_drvdata(dev);
	unsigned int val = 0;
	int ret;

	if (!data || !data->regmap)
		return -ENODEV;

	ret = regmap_read(data->regmap, PM8350B_TYPEC_MODE_CFG_REG, &val);
	if (ret)
		return ret;

	if (sysfs_streq(buf, "snk") || sysfs_streq(buf, "sink") || sysfs_streq(buf, "sink_only")) {
		val |= (PM8350B_EN_TRY_SNK_BIT | PM8350B_EN_SNK_ONLY_BIT);
	} else if (sysfs_streq(buf, "drp") || sysfs_streq(buf, "try_snk")) {
		val &= ~PM8350B_EN_SNK_ONLY_BIT;
		val |= PM8350B_EN_TRY_SNK_BIT;
	} else {
		unsigned int custom;
		if (kstrtouint(buf, 0, &custom))
			return -EINVAL;
		val = custom;
	}

	ret = regmap_write(data->regmap, PM8350B_TYPEC_MODE_CFG_REG, val);
	if (ret)
		return ret;

	pr_info("Type-C mode cfg set to 0x%02x\n", val);
	return count;
}
static DEVICE_ATTR_RW(typec_mode);

static struct attribute *pm8350b_attrs[] = {
	&dev_attr_otg_enable.attr,
	&dev_attr_usb_suspend.attr,
	&dev_attr_typec_mode.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pm8350b);

static const struct ucsi_power_ops pm8350b_otg_ops = {
	.disable_otg = pm8350b_disable_otg,
	.enable_otg  = pm8350b_enable_otg,
	.is_power_supplied = pm8350b_is_power_supplied,
};

static int pmic_spmi_probe(struct spmi_device *sdev)
{
	struct device_node *root = sdev->dev.of_node;
	struct regmap *regmap;
	int ret;

	if (of_property_read_bool(root, "qcom,can-sleep"))
		regmap = devm_regmap_init_spmi_ext(sdev,
						&spmi_regmap_can_sleep_config);
	else
		regmap = devm_regmap_init_spmi_ext(sdev, &spmi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	devm_regmap_qti_debugfs_register(&sdev->dev, regmap);

	/* Only the first slave id for a PMIC contains this information */
	if (sdev->usid % 2 == 0)
		pmic_spmi_show_revid(regmap, &sdev->dev);

	if (sdev->usid == PM8350B_USID &&
	    of_device_is_compatible(sdev->ctrl->dev.of_node, "qcom,spmi-pmic-arb")) {
		struct pm8350b_data *data;

		pm8350b_spmi_regmap = regmap;

		/*
		 *
		 * 1. Default USB input to unsuspended (0x2954 = 0x00) on initial boot.
		 * 2. Cut OTG Boost (0x2740 = 0x00) and autonomous boost (0x2753 bit 1 = 0),
		 *    and set Try.SNK (0x2b44 = 0x10)
		 *    for Host data role negotiation while drawing charging current.
		 */
		pm8350b_enable_otg();
		ucsi_register_power_ops(&pm8350b_otg_ops);

		data = devm_kzalloc(&sdev->dev, sizeof(*data), GFP_KERNEL);
		if (data) {
			data->dev = &sdev->dev;
			data->regmap = regmap;
			mutex_init(&data->lock);
			dev_set_drvdata(&sdev->dev, data);

			pm8350b_global_data = data;

			ret = sysfs_create_groups(&sdev->dev.kobj, pm8350b_groups);
			if (ret)
				dev_warn(&sdev->dev, "Failed to create sysfs groups: %d\n", ret);

			ret = sysfs_create_link(kernel_kobj, &sdev->dev.kobj, "pm8350b");
			if (ret)
				dev_warn(&sdev->dev, "Failed to create /sys/kernel/pm8350b link: %d\n", ret);
		}

		pr_info("PM8350B: power controls initialized (OTG off, sink-only 0x12, USB unsuspended)\n");
	}

	return devm_of_platform_populate(&sdev->dev);
}

MODULE_DEVICE_TABLE(of, pmic_spmi_id_table);

static void pmic_spmi_remove(struct spmi_device *sdev)
{
	if (sdev->usid == PM8350B_USID &&
	    of_device_is_compatible(sdev->ctrl->dev.of_node, "qcom,spmi-pmic-arb")) {
		struct pm8350b_data *data = dev_get_drvdata(&sdev->dev);
		cancel_delayed_work_sync(&pm8350b_unsuspend_dwork);
		ucsi_unregister_power_ops();
		pm8350b_spmi_regmap = NULL;

		if (data) {
			pm8350b_global_data = NULL;
			sysfs_remove_link(kernel_kobj, "pm8350b");
			sysfs_remove_groups(&sdev->dev.kobj, pm8350b_groups);
		}
	}
}

static struct spmi_driver pmic_spmi_driver = {
	.probe = pmic_spmi_probe,
	.remove = pmic_spmi_remove,
	.driver = {
		.name = "pmic-spmi",
		.of_match_table = pmic_spmi_id_table,
	},
};

static int __init pmic_spmi_init(void)
{
	return spmi_driver_register(&pmic_spmi_driver);
}
arch_initcall(pmic_spmi_init);

static void __exit pmic_spmi_exit(void)
{
	spmi_driver_unregister(&pmic_spmi_driver);
}
module_exit(pmic_spmi_exit);

MODULE_DESCRIPTION("Qualcomm SPMI PMIC driver");
MODULE_ALIAS("spmi:spmi-pmic");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Josh Cartwright <joshc@codeaurora.org>");
MODULE_AUTHOR("Stanimir Varbanov <svarbanov@mm-sol.com>");
