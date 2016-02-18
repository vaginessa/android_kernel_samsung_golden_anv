/*
 * RTC subsystem, base class
 *
 * Copyright (C) 2005 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * class skeleton from drivers/hwmon/hwmon.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "rtc-core.h"


static DEFINE_IDR(rtc_idr);
static DEFINE_MUTEX(idr_lock);
struct class *rtc_class;

int rtc_delta = 1;
EXPORT_SYMBOL(rtc_delta);

static void rtc_device_release(struct device *dev)
{
	struct rtc_device *rtc = to_rtc_device(dev);
	mutex_lock(&idr_lock);
	idr_remove(&rtc_idr, rtc->id);
	mutex_unlock(&idr_lock);
	kfree(rtc);
}

#if defined(CONFIG_PM) && defined(CONFIG_RTC_HCTOSYS_DEVICE)

/*
 * On suspend(), measure the delta between one RTC and the
 * system's wall clock; restore it on resume().
 */

static struct timespec	delta;
static struct timespec	delta_delta;
static time_t		oldtime;

static int rtc_suspend(struct device *dev, pm_message_t mesg)
{
	struct rtc_device	*rtc = to_rtc_device(dev);
	struct rtc_time		tm;
	struct timespec		ts;
	struct timespec		new_delta;

#ifdef CONFIG_MACH_SAMSUNG_U8500
	struct rtc_time         sys_tm;
	struct rtc_time         aprtc_tm;
	struct timespec         aprtc_ts;
#endif /* CONFIG_MACH_SAMSUNG_U8500 */
	if (strcmp(dev_name(&rtc->dev), CONFIG_RTC_HCTOSYS_DEVICE) != 0)
		return 0;

	getnstimeofday(&ts);
	rtc_read_time(rtc, &tm);
	rtc_tm_to_time(&tm, &oldtime);

#ifdef CONFIG_MACH_SAMSUNG_U8500
	read_persistent_clock(&aprtc_ts);
	rtc_time_to_tm(aprtc_ts.tv_sec, &aprtc_tm);
	rtc_time_to_tm(ts.tv_sec, &sys_tm);

	pr_info("[%s] AP RTC TIME: %04d.%02d.%02d - %02d:%02d:%02d called\n",
		__func__, aprtc_tm.tm_year+1900, aprtc_tm.tm_mon+1, aprtc_tm.tm_mday,
		aprtc_tm.tm_hour, aprtc_tm.tm_min, aprtc_tm.tm_sec);

	pr_info("[%s] PMIC RTC TIME: %04d.%02d.%02d - %02d:%02d:%02d called\n",
		__func__, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);

	pr_info("[%s] SYSTEM TIME: %04d.%02d.%02d - %02d:%02d:%02d called\n",
		__func__, sys_tm.tm_year+1900, sys_tm.tm_mon+1, sys_tm.tm_mday,
		sys_tm.tm_hour, sys_tm.tm_min, sys_tm.tm_sec);
#endif /* CONFIG_MACH_SAMSUNG_U8500 */

	/* RTC precision is 1 second; adjust delta for avg 1/2 sec err */
	set_normalized_timespec(&new_delta,
				ts.tv_sec - oldtime,
				ts.tv_nsec - (NSEC_PER_SEC >> 1));

	/* prevent 1/2 sec errors from accumulating */
	delta_delta = timespec_sub(new_delta, delta);
	if (delta_delta.tv_sec < -2 || delta_delta.tv_sec >= 2)
		delta = new_delta;
	return 0;
}

static int rtc_resume(struct device *dev)
{
	struct rtc_device	*rtc = to_rtc_device(dev);
	struct rtc_time		tm;
	time_t			newtime;
	struct timespec		time;
#ifdef CONFIG_MACH_SAMSUNG_U8500
	struct rtc_time		sys_tm;
	struct rtc_time		aprtc_tm;
	struct timespec		aprtc_ts;
#endif /* CONFIG_MACH_SAMSUNG_U8500 */

	if (strcmp(dev_name(&rtc->dev), CONFIG_RTC_HCTOSYS_DEVICE) != 0)
		return 0;

#ifdef CONFIG_MACH_SAMSUNG_U8500
	read_persistent_clock(&aprtc_ts);
#endif /* CONFIG_MACH_SAMSUNG_U8500 */
	rtc_read_time(rtc, &tm);
	if (rtc_valid_tm(&tm) != 0) {
		pr_debug("%s:  bogus resume time\n", dev_name(&rtc->dev));
		return 0;
	}
	rtc_tm_to_time(&tm, &newtime);
	if (delta_delta.tv_sec < -1)
		newtime++;
	if (newtime <= oldtime) {
		if (newtime < oldtime)
		pr_debug("%s:  time travel!\n", dev_name(&rtc->dev));
		return 0;
	}

	/* restore wall clock using delta against this RTC;
	 * adjust again for avg 1/2 second RTC sampling error
	 */
	set_normalized_timespec(&time,
				newtime + delta.tv_sec,
				(NSEC_PER_SEC >> 1) + delta.tv_nsec);
	do_settimeofday(&time);
#ifdef CONFIG_MACH_SAMSUNG_U8500
	rtc_time_to_tm(time.tv_sec, &sys_tm);
	rtc_time_to_tm(aprtc_ts.tv_sec, &aprtc_tm);
	pr_info("[%s] AP RTC TIME: %04d.%02d.%02d - %02d:%02d:%02d called\n",
		__func__,
		aprtc_tm.tm_year+1900, aprtc_tm.tm_mon+1, aprtc_tm.tm_mday,
		aprtc_tm.tm_hour, aprtc_tm.tm_min, aprtc_tm.tm_sec);

	pr_info("[%s] PMIC RTC TIME: %04d.%02d.%02d - %02d:%02d:%02d called\n",
		__func__, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);

	pr_info("[%s] SYSTEM TIME: %04d.%02d.%02d - %02d:%02d:%02d called\n",
		__func__, sys_tm.tm_year+1900, sys_tm.tm_mon+1, sys_tm.tm_mday,
		sys_tm.tm_hour, sys_tm.tm_min, sys_tm.tm_sec);

	pr_info("[%s] delta.tv_sec: 0x%llx delta.tv_nsec: 0x%llx\n", __func__,
		(unsigned long long)delta.tv_sec, (unsigned long long)delta.tv_nsec);
	pr_info("[%s] delta_delta.tv_sec: 0x%llx delta_delta.tv_nsec: 0x%llx\n",
		__func__, (unsigned long long)delta_delta.tv_sec, (unsigned long long)delta_delta.tv_nsec);

	rtc_delta = delta.tv_sec;

#endif /* CONFIG_MACH_SAMSUNG_U8500 */

	return 0;
}

#else
#define rtc_suspend	NULL
#define rtc_resume	NULL
#endif


/**
 * rtc_device_register - register w/ RTC class
 * @dev: the device to register
 *
 * rtc_device_unregister() must be called when the class device is no
 * longer needed.
 *
 * Returns the pointer to the new struct class device.
 */
struct rtc_device *rtc_device_register(const char *name, struct device *dev,
					const struct rtc_class_ops *ops,
					struct module *owner)
{
	struct rtc_device *rtc;
	struct rtc_wkalrm alrm;
	int id, err;

	if (idr_pre_get(&rtc_idr, GFP_KERNEL) == 0) {
		err = -ENOMEM;
		goto exit;
	}


	mutex_lock(&idr_lock);
	err = idr_get_new(&rtc_idr, NULL, &id);
	mutex_unlock(&idr_lock);

	if (err < 0)
		goto exit;

	id = id & MAX_ID_MASK;

	rtc = kzalloc(sizeof(struct rtc_device), GFP_KERNEL);
	if (rtc == NULL) {
		err = -ENOMEM;
		goto exit_idr;
	}

	rtc->id = id;
	rtc->ops = ops;
	rtc->owner = owner;
	rtc->irq_freq = 1;
	rtc->max_user_freq = 64;
	rtc->dev.parent = dev;
	rtc->dev.class = rtc_class;
	rtc->dev.release = rtc_device_release;

	mutex_init(&rtc->ops_lock);
	spin_lock_init(&rtc->irq_lock);
	spin_lock_init(&rtc->irq_task_lock);
	init_waitqueue_head(&rtc->irq_queue);

	/* Init timerqueue */
	timerqueue_init_head(&rtc->timerqueue);
	INIT_WORK(&rtc->irqwork, rtc_timer_do_work);
	/* Init aie timer */
	rtc_timer_init(&rtc->aie_timer, rtc_aie_update_irq, (void *)rtc);
	/* Init uie timer */
	rtc_timer_init(&rtc->uie_rtctimer, rtc_uie_update_irq, (void *)rtc);
	/* Init pie timer */
	hrtimer_init(&rtc->pie_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	rtc->pie_timer.function = rtc_pie_update_irq;
	rtc->pie_enabled = 0;

	/* Check to see if there is an ALARM already set in hw */
	err = __rtc_read_alarm(rtc, &alrm);

	if (!err && !rtc_valid_tm(&alrm.time))
		rtc_initialize_alarm(rtc, &alrm);

	strlcpy(rtc->name, name, RTC_DEVICE_NAME_SIZE);
	dev_set_name(&rtc->dev, "rtc%d", id);

	rtc_dev_prepare(rtc);

	err = device_register(&rtc->dev);
	if (err) {
		put_device(&rtc->dev);
		goto exit_kfree;
	}

	rtc_dev_add_device(rtc);
	rtc_sysfs_add_device(rtc);
	rtc_proc_add_device(rtc);

	dev_info(dev, "rtc core: registered %s as %s\n",
			rtc->name, dev_name(&rtc->dev));

	return rtc;

exit_kfree:
	kfree(rtc);

exit_idr:
	mutex_lock(&idr_lock);
	idr_remove(&rtc_idr, id);
	mutex_unlock(&idr_lock);

exit:
	dev_err(dev, "rtc core: unable to register %s, err = %d\n",
			name, err);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(rtc_device_register);


/**
 * rtc_device_unregister - removes the previously registered RTC class device
 *
 * @rtc: the RTC class device to destroy
 */
void rtc_device_unregister(struct rtc_device *rtc)
{
	if (get_device(&rtc->dev) != NULL) {
		mutex_lock(&rtc->ops_lock);
		/* remove innards of this RTC, then disable it, before
		 * letting any rtc_class_open() users access it again
		 */
		rtc_sysfs_del_device(rtc);
		rtc_dev_del_device(rtc);
		rtc_proc_del_device(rtc);
		device_unregister(&rtc->dev);
		rtc->ops = NULL;
		mutex_unlock(&rtc->ops_lock);
		put_device(&rtc->dev);
	}
}
EXPORT_SYMBOL_GPL(rtc_device_unregister);

static int __init rtc_init(void)
{
	rtc_class = class_create(THIS_MODULE, "rtc");
	if (IS_ERR(rtc_class)) {
		printk(KERN_ERR "%s: couldn't create class\n", __FILE__);
		return PTR_ERR(rtc_class);
	}
	rtc_class->suspend = rtc_suspend;
	rtc_class->resume = rtc_resume;
	rtc_dev_init();
	rtc_sysfs_init(rtc_class);
	return 0;
}

static void __exit rtc_exit(void)
{
	rtc_dev_exit();
	class_destroy(rtc_class);
	idr_destroy(&rtc_idr);
}

subsys_initcall(rtc_init);
module_exit(rtc_exit);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("RTC class support");
MODULE_LICENSE("GPL");