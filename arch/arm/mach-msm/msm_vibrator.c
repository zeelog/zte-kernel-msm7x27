/* include/asm/mach-msm/htc_pwrsink.h
 *
 * Copyright (C) 2008 HTC Corporation.
 * Copyright (C) 2007 Google, Inc.
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
/*histstory:
 when       who     what, where, why                                            comment tag
 --------   ----    ---------------------------------------------------    ----------------------------------
 2009-10-23    hp    mermge vibrator support                                       ZTE_VIBRATOR_HP_01

*/
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <../../../drivers/staging/android/timed_output.h>
#include <linux/sched.h>

#include <mach/msm_rpcrouter.h>

#define PM_LIBPROG	  0x30000061
//ZTE_VIBRATOR_HP_01 2009-10-23 
//change rpc ver number
#if 1   //for ZTE Raise 7x27
#define PM_LIBVERS      0x10001
//ZTE_VIBRATOR_HP_01 end 
#else
#if (CONFIG_MSM_AMSS_VERSION == 6220) || (CONFIG_MSM_AMSS_VERSION == 6225)
#define PM_LIBVERS	  0xfb837d0b
#else
#define PM_LIBVERS	  MSM_RPC_VERS(1,1)
#endif
#endif

//ZTE_VIBRATOR_HP_01 2009-10-23 
//change rpc proc id 
#define HTC_PROCEDURE_SET_VIB_ON_OFF	22
//ZTE_VIBRATOR_HP_01 end 
#define PMIC_VIBRATOR_LEVEL_MAX  3100
#define PMIC_VIBRATOR_LEVEL_MIN  1100

//add by stone 2010_0301
//#define STONE_DEBUG

static struct work_struct vibrator_work;
static struct hrtimer vibe_timer;
static spinlock_t vibe_lock;
static int vibe_state;

/* default value for vibration intensity, 95% = 3000 in PMIC_VIBRATOR_LEVEL */
static unsigned long pwmval = 95;

static void set_pmic_vibrator(int on)
{
	int pwm_duty;
	static struct msm_rpc_endpoint *vib_endpoint;
	struct set_vib_on_off_req {
		struct rpc_request_hdr hdr;
		uint32_t data;
	} req;

#ifdef STONE_DEBUG
        printk("\nstone vib01:0x%x,0x%x\n",on,vib_endpoint); 
#endif

	if (!vib_endpoint) {
		vib_endpoint = msm_rpc_connect(PM_LIBPROG, PM_LIBVERS, 0);
		if (IS_ERR(vib_endpoint)) {
			printk(KERN_ERR "init vib rpc failed!\n");
			vib_endpoint = 0;
			return;
		}
	}

	/* make sure pwmval is between 0 and 100 */
	if (pwmval > 100) {
		pwmval = 100;
	} else if (pwmval < 0) {
		pwmval = 0;
	}

	/* calculate vibration level */
	pwm_duty = (PMIC_VIBRATOR_LEVEL_MIN +
	(pwmval * (PMIC_VIBRATOR_LEVEL_MAX - PMIC_VIBRATOR_LEVEL_MIN) / 100));

	if (on)
		req.data = cpu_to_be32(pwm_duty);
	else
		req.data = cpu_to_be32(0);

	msm_rpc_call(vib_endpoint, HTC_PROCEDURE_SET_VIB_ON_OFF, &req,
		sizeof(req), 5 * HZ);
}

static void update_vibrator(struct work_struct *work)
{
	set_pmic_vibrator(vibe_state);
}

static void vibrator_enable(struct timed_output_dev *dev, int value)
{
	unsigned long	flags;

	spin_lock_irqsave(&vibe_lock, flags);
	hrtimer_cancel(&vibe_timer);

#ifdef STONE_DEBUG
         printk("\nstone vib02:0x%x,0x%x\n",dev,value); 
#endif

	if (value == 0)
		vibe_state = 0;
	else {
		//ZTE_VIBRATOR_HP_01 2009-10-23
		//change max duration time to 1000
		value = (value > 1000 ? 1000 : value);
		//ZTE_VIBRATOR_HP_01 end
		vibe_state = 1;
		hrtimer_start(&vibe_timer,
			ktime_set(value / 1000, (value % 1000) * 1000000),
			HRTIMER_MODE_REL);
	}
	spin_unlock_irqrestore(&vibe_lock, flags);

	schedule_work(&vibrator_work);
}

static int vibrator_get_time(struct timed_output_dev *dev)
{
	if (hrtimer_active(&vibe_timer)) {
		ktime_t r = hrtimer_get_remaining(&vibe_timer);
		return r.tv.sec * 1000 + r.tv.nsec / 1000000;
	} else
		return 0;
}

static enum hrtimer_restart vibrator_timer_func(struct hrtimer *timer)
{
	vibe_state = 0;
	schedule_work(&vibrator_work);
	return HRTIMER_NORESTART;
}

static ssize_t pwmvalue_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int count;

	count = sprintf(buf, "%lu\n", pwmval);
	pr_info("vibrator: pwmval: %lu\n", pwmval);

	return count;
}

ssize_t pwmvalue_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	if (sscanf(buf, "%lu", &pwmval) != 1)
		pr_err("vibrator: error in storing pwm value\n");

	pr_info("vibrator: pwmval: %lu\n", pwmval);

	return size;
}

static DEVICE_ATTR(pwmvalue, S_IRUGO | S_IWUGO,
		pwmvalue_show, pwmvalue_store);

static int blade_create_vibrator_sysfs(void)
{
	int ret;
	struct kobject *vibrator_kobj;
	vibrator_kobj = kobject_create_and_add("vibrator", NULL);
	if (unlikely(!vibrator_kobj))
	return -ENOMEM;

	ret = sysfs_create_file(vibrator_kobj,
			&dev_attr_pwmvalue.attr);
	if (unlikely(ret < 0)) {
		pr_err("vibrator: sysfs_create_file failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct timed_output_dev pmic_vibrator = {
	.name = "vibrator",
	.get_time = vibrator_get_time,
	.enable = vibrator_enable,
};

void __init msm_init_pmic_vibrator(void)
{
	INIT_WORK(&vibrator_work, update_vibrator);
	
//#ifdef STONE_DEBUG
         printk("\nstone_20100302 vib00:**************************************\n"); 
//#endif


	spin_lock_init(&vibe_lock);
	vibe_state = 0;
	hrtimer_init(&vibe_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vibe_timer.function = vibrator_timer_func;

	blade_create_vibrator_sysfs();

	timed_output_dev_register(&pmic_vibrator);
}

MODULE_DESCRIPTION("timed output pmic vibrator device");
MODULE_LICENSE("GPL");

