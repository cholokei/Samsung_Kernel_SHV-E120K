/*
 *  STMicroelectronics k3dh acceleration sensor driver
 *
 *  Copyright (C) 2010 Samsung Electronics Co.Ltd
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/i2c/k3dh.h>
#include "k3dh_reg.h"
#include <mach/gpio.h>


#define k3dh_dbgmsg(str, args...) pr_debug("%s: " str, __func__, ##args)

/* The default settings when sensor is on is for all 3 axis to be enabled
 * and output data rate set to 400Hz.  Output is via a ioctl read call.
 */
#define DEFAULT_POWER_ON_SETTING (ODR400 | ENABLE_ALL_AXES)
#define ACC_DEV_NAME "accelerometer"
#define ACC_DEV_MAJOR 103 /* 241 */

#define CALIBRATION_FILE_PATH	"/efs/calibration_data"
#define CALIBRATION_DATA_AMOUNT	20

static const struct odr_delay {
	u8 odr; /* odr reg setting */
	s64 delay_ns; /* odr in ns */
} odr_delay_table[] = {
	{ ODR1344,     744047LL }, /* 1344Hz */
	{  ODR400,    2500000LL }, /*  400Hz */
	{  ODR200,    5000000LL }, /*  200Hz */
	{  ODR100,   10000000LL }, /*  100Hz */
	{   ODR50,   20000000LL }, /*   50Hz */
	{   ODR25,   40000000LL }, /*   25Hz */
	{   ODR10,  100000000LL }, /*   10Hz */
	{    ODR1, 1000000000LL }, /*    1Hz */
};

/* K3DH acceleration data */
struct k3dh_acc {
	s16 x;
	s16 y;
	s16 z;
};

struct k3dh_data {
	struct i2c_client *client;
	struct miscdevice k3dh_device;
	struct k3dh_platform_data *pdata;
	struct mutex read_lock;
	struct mutex write_lock;
	struct completion data_ready;
	struct class *acc_class;
	struct k3dh_acc cal_data;
	u8 ctrl_reg1_shadow;
	atomic_t opened; /* opened implies enabled */
	void	(*power_on) (void);
	void	(*power_off) (void);
};

extern struct class *sec_class;

#if defined (CONFIG_KOR_MODEL_SHV_E120L)||defined (CONFIG_KOR_MODEL_SHV_E120S)||defined(CONFIG_KOR_MODEL_SHV_E120K)
#define SENSE_SCL	52
#define SENSE_SDA	51

int gpio_request_count = 0;
int gpio_free_count = 0;
#endif

/* Read X,Y and Z-axis acceleration raw data */
static int k3dh_read_accel_raw_xyz(struct k3dh_data *k3dh, struct k3dh_acc *acc)
{
	int err;
	s8 reg = OUT_X_L | AC; /* read from OUT_X_L to OUT_Z_H by auto-inc */
	u8 acc_data[6];

	err = i2c_smbus_read_i2c_block_data(k3dh->client, reg, sizeof(acc_data), acc_data);
	if (err != sizeof(acc_data)) {
		pr_err("%s : failed to read 6 bytes for getting x/y/z\n", __func__);
		return -EIO;
    }

	acc->x = (acc_data[1] << 8) | acc_data[0];
	acc->y = (acc_data[3] << 8) | acc_data[2];
	acc->z = (acc_data[5] << 8) | acc_data[4];

	acc->x = acc->x >> 4;
	acc->y = acc->y >> 4;
	acc->z = acc->z >> 4;

	return 0;
}

#if defined (CONFIG_TARGET_LOCALE_KOR) || defined (CONFIG_JPN_MODEL_SC_03D)|| defined(CONFIG_USA_MODEL_SGH_I727)|| defined(CONFIG_USA_MODEL_SGH_T989) \
 || defined(CONFIG_USA_MODEL_SGH_I717) || defined(CONFIG_EUR_MODEL_GT_I9210) || defined(CONFIG_USA_MODEL_SGH_I957) \
 || defined(CONFIG_USA_MODEL_SGH_I757)|| defined (CONFIG_USA_MODEL_SGH_T769) || defined(CONFIG_USA_MODEL_SGH_I577)
extern unsigned int get_hw_rev(void);
#endif

static int k3dh_read_accel_xyz(struct k3dh_data *k3dh, struct k3dh_acc *acc)
{
	int err = 0;
	static int k3dh_read_count = 0;

	mutex_lock(&k3dh->read_lock);
	err = k3dh_read_accel_raw_xyz(k3dh, acc);
	mutex_unlock(&k3dh->read_lock);
	if (err < 0) {
		pr_err("[ACC] %s: k3dh_read_accel_raw_xyz() failed\n", __func__);
		return err;
	}

	acc->x -= k3dh->cal_data.x;
	acc->y -= k3dh->cal_data.y;
	acc->z -= k3dh->cal_data.z;

#if defined (CONFIG_TARGET_LOCALE_KOR)
#if defined (CONFIG_KOR_MODEL_SHV_E110S)
	if (get_hw_rev() >= 0x06 )
	{
		acc->y = -(acc->y);
		acc->z = -(acc->z);
	}
	else if (get_hw_rev() >= 0x05 )
	{
		acc->x = -(acc->x);
		acc->y = -(acc->y);
		acc->z = -(acc->z);
	}
	else
#elif defined (CONFIG_KOR_MODEL_SHV_E120L)
	if (get_hw_rev() >= 0x01 )
	{
		acc->x = -(acc->x);
		acc->y = (acc->y);
		acc->z = -(acc->z);
	}
	else		
#elif defined (CONFIG_KOR_MODEL_SHV_E120S) || defined (CONFIG_KOR_MODEL_SHV_E120K)
	{
		s16 temp = acc->x;
		acc->x = acc->y;
		acc->y = temp;
		acc->z = -(acc->z);
	}
#elif defined (CONFIG_KOR_MODEL_SHV_E160S) || defined(CONFIG_KOR_MODEL_SHV_E160K) || defined(CONFIG_KOR_MODEL_SHV_E160L)	
	if (get_hw_rev() >= 0x02 )
	{
		acc->x = -(acc->x);
		acc->y = -(acc->y);
	}
#endif

#if !defined(CONFIG_KOR_MODEL_SHV_E160S) && !defined (CONFIG_KOR_MODEL_SHV_E160K) && !defined(CONFIG_KOR_MODEL_SHV_E160L)
	if (get_hw_rev() >= 0x04 )
	{
		s16 temp = acc->x;
		acc->x = acc->y;
		acc->y = temp;
		acc->z = -(acc->z);
	}
	else if (get_hw_rev() !=0x01 )
	{
		acc->x = -(acc->x);
		acc->y = -(acc->y);
 	}
#endif
#elif defined (CONFIG_JPN_MODEL_SC_03D)	
	if (get_hw_rev() >= 0x00 ) // real 0.0
	{
		acc->x = -(acc->x);
		acc->y = (acc->y);
		acc->z = -(acc->z);
	}
#elif defined(CONFIG_EUR_MODEL_GT_I9210)
	{
		s16 temp = acc->x;
		acc->x = (acc->y);
		acc->y = temp;
		acc->z = -(acc->z);
	}
#elif defined (CONFIG_USA_MODEL_SGH_I577)
	{
		acc->x = -(acc->x);
		acc->y = -(acc->y);
	}
#elif defined (CONFIG_USA_MODEL_SGH_I727)
	if (get_hw_rev() >= 0x04 ) 
	{
		acc->x = -(acc->x);
		acc->z = -(acc->z);
	}
#elif defined (CONFIG_USA_MODEL_SGH_I717)
	if (true) 
	{
		s16 temp = acc->x;
		acc->x = (acc->y);
		acc->y = (temp);
		acc->z = -(acc->z);
	}    
#elif defined (CONFIG_USA_MODEL_SGH_I757)
	if (true) 
	{
		s16 temp = acc->x;
		acc->x = -(acc->y);
		acc->y = (temp);
		acc->z = (acc->z);
	}
#elif defined (CONFIG_USA_MODEL_SGH_T769)
	{
		acc->x = -(acc->x);
		acc->y = -(acc->y);
	}    
#elif defined (CONFIG_USA_MODEL_SGH_T989)
	{
	#if defined(CONFIG_USA_MODEL_SGH_T989D)
		acc->x = -(acc->x);
		acc->y = acc->y;
                acc->z = -(acc->z);
	#else
                s16 temp = acc->x;
                acc->x = -(acc->y);
                acc->y = (temp);
                acc->z = (acc->z);	
	#endif
	}
#endif

	/* For Debug */
	if(k3dh_read_count++ == 500) // each 10 seconds
	{
		k3dh_read_count = 0;
		printk("%s: data=(%d, %d, %d) cal=(%d, %d, %d)\n", __func__, acc->x, acc->y, acc->z, k3dh->cal_data.x, k3dh->cal_data.y, k3dh->cal_data.z);
	}

	return err;
}

static int k3dh_open_calibration(struct k3dh_data *k3dh)
{
	struct file *cal_filp = NULL;
	int err = 0;
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cal_filp = filp_open(CALIBRATION_FILE_PATH, O_RDONLY, 0666);
	if (IS_ERR(cal_filp)) {
		pr_err("[ACC] %s: Can't open calibration file\n", __func__);
		set_fs(old_fs);
		err = PTR_ERR(cal_filp);
		return err;
	}

	err = cal_filp->f_op->read(cal_filp, (char *)&k3dh->cal_data, 3 * sizeof(s16), &cal_filp->f_pos);
	if (err != 3 * sizeof(s16)) {
		pr_err("[ACC] %s: Can't read the cal data from file\n", __func__);
		err = -EIO;
	}

	k3dh_dbgmsg("[ACC] %s: (%u,%u,%u)\n", __func__, k3dh->cal_data.x, k3dh->cal_data.y, k3dh->cal_data.z);
	filp_close(cal_filp, current->files);
	set_fs(old_fs);

	return err;
}

static int k3dh_do_calibrate(struct device *dev, int is_cal_erase)
{
	struct k3dh_data *k3dh = dev_get_drvdata(dev);
	struct k3dh_acc data = { 0, };
	struct file *cal_filp = NULL;
	int sum[3] = { 0, };
	int err = 0;
	int i;
	mm_segment_t old_fs;

	if(is_cal_erase) {
		k3dh->cal_data.x = 0;
		k3dh->cal_data.y = 0;
		k3dh->cal_data.z = 0;
	}
	else {
		for (i = 0; i < CALIBRATION_DATA_AMOUNT; i++) {
			mutex_lock(&k3dh->read_lock);
			err = k3dh_read_accel_raw_xyz(k3dh, &data);
			mutex_unlock(&k3dh->read_lock);
			if (err < 0) {
                pr_err("[ACC] %s: k3dh_read_accel_raw_xyz()" "failed in the %dth loop\n", __func__, i);
                return err;
			}
			sum[0] += data.x;
			sum[1] += data.y;
			sum[2] += data.z;
		}

		k3dh->cal_data.x = sum[0] / CALIBRATION_DATA_AMOUNT;
		k3dh->cal_data.y = sum[1] / CALIBRATION_DATA_AMOUNT;
		if(sum[2] > 0)
			k3dh->cal_data.z = (sum[2] / CALIBRATION_DATA_AMOUNT) - 1024;
		else
			k3dh->cal_data.z = (sum[2] / CALIBRATION_DATA_AMOUNT) + 1024;
	}

	printk(KERN_INFO "[ACC] %s: cal data (%d,%d,%d)\n", __func__,	k3dh->cal_data.x, k3dh->cal_data.y, k3dh->cal_data.z);

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cal_filp = filp_open(CALIBRATION_FILE_PATH, O_CREAT | O_TRUNC | O_WRONLY, 0666);
	if (IS_ERR(cal_filp)) {
		pr_err("[ACC] %s: Can't open calibration file\n", __func__);
		set_fs(old_fs);
		err = PTR_ERR(cal_filp);
		return err;
	}

	err = cal_filp->f_op->write(cal_filp, (char *)&k3dh->cal_data, 3 * sizeof(s16), &cal_filp->f_pos);
	if (err != 3 * sizeof(s16)) {
		pr_err("[ACC] %s: Can't write the cal data to file\n", __func__);
		err = -EIO;
	}

	filp_close(cal_filp, current->files);
	set_fs(old_fs);
	return err;
}

/*  open command for K3DH device file  */
static int k3dh_open(struct inode *inode, struct file *file)
{
	int err = 0;
	struct k3dh_data *k3dh = container_of(file->private_data, struct k3dh_data, k3dh_device);
	
	file->private_data = k3dh;
	if (atomic_read(&k3dh->opened) == 0) {
		err = k3dh_open_calibration(k3dh);
		if (err < 0)
			pr_err("[ACC] %s: k3dh_open_calibration() failed\n", __func__);
        
		k3dh->ctrl_reg1_shadow = DEFAULT_POWER_ON_SETTING;
		err = i2c_smbus_write_byte_data(k3dh->client, CTRL_REG1, DEFAULT_POWER_ON_SETTING);
		if (err)
			pr_err("[ACC] %s: i2c write ctrl_reg1 failed\n", __func__);

		err = i2c_smbus_write_byte_data(k3dh->client, CTRL_REG4, CTRL_REG4_HR);
		if (err)
			pr_err("[ACC] %s: i2c write ctrl_reg4 failed\n", __func__);
	}
	atomic_add(1, &k3dh->opened);
	return err;
}

/*  release command for K3DH device file */
static int k3dh_close(struct inode *inode, struct file *file)
{
	int err = 0;
	struct k3dh_data *k3dh = file->private_data;

	atomic_sub(1, &k3dh->opened);
	if (atomic_read(&k3dh->opened) == 0) {
		err = i2c_smbus_write_byte_data(k3dh->client, CTRL_REG1, PM_OFF);
		k3dh->ctrl_reg1_shadow = PM_OFF;
	}

	return err;
}

static s64 k3dh_get_delay(struct k3dh_data *k3dh)
{
	int i;
	u8 odr;
	s64 delay = -1;

	odr = k3dh->ctrl_reg1_shadow & ODR_MASK;
	for (i = 0; i < ARRAY_SIZE(odr_delay_table); i++) {
		if (odr == odr_delay_table[i].odr) {
			delay = odr_delay_table[i].delay_ns;
			break;
		}
	}
	return delay;
}

static int k3dh_set_delay(struct k3dh_data *k3dh, s64 delay_ns)
{
	int odr_value = ODR1;
	int res = 0;
	int i;
	/* round to the nearest delay that is less than
	 * the requested value (next highest freq)
	 */
	k3dh_dbgmsg(" passed %lldns\n", delay_ns);
	for (i = 0; i < ARRAY_SIZE(odr_delay_table); i++) {
		if (delay_ns < odr_delay_table[i].delay_ns)
			break;
	}
	if (i > 0)
		i--;
	k3dh_dbgmsg("matched rate %lldns, odr = 0x%x\n", odr_delay_table[i].delay_ns, odr_delay_table[i].odr);
	odr_value = odr_delay_table[i].odr;
	delay_ns = odr_delay_table[i].delay_ns;
	mutex_lock(&k3dh->write_lock);
	k3dh_dbgmsg("old = %lldns, new = %lldns\n", k3dh_get_delay(k3dh), delay_ns);
    
	if (odr_value != (k3dh->ctrl_reg1_shadow & ODR_MASK)) {
		u8 ctrl = (k3dh->ctrl_reg1_shadow & ~ODR_MASK);
		ctrl |= odr_value;
		k3dh->ctrl_reg1_shadow = ctrl;
		res = i2c_smbus_write_byte_data(k3dh->client, CTRL_REG1, ctrl);
		if(res) {
			pr_err("%s : failed to write CTRL_REG1 res=%d\n", __func__, res);
		}
			
		k3dh_dbgmsg("writing odr value 0x%x\n", odr_value);
	}
	mutex_unlock(&k3dh->write_lock);
	return res;
}

/*  ioctl command for K3DH device file */
static long k3dh_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct k3dh_data *k3dh = file->private_data;
	struct k3dh_acc data;
	s64 delay_ns;

	/* cmd mapping */
	switch (cmd) {
	case K3DH_IOCTL_SET_DELAY:
		if (copy_from_user(&delay_ns, (void __user *)arg, sizeof(delay_ns)))
			return -EFAULT;
		err = k3dh_set_delay(k3dh, delay_ns);
		break;
	case K3DH_IOCTL_GET_DELAY:
		delay_ns = k3dh_get_delay(k3dh);
		if (put_user(delay_ns, (s64 __user *)arg))
			return -EFAULT;
		break;
	case K3DH_IOCTL_READ_ACCEL_XYZ:
//		return 0; //umfa
		err = k3dh_read_accel_xyz(k3dh, &data);
		if (err)
			break;
		if (copy_to_user((void __user *)arg, &data, sizeof(data)))
			return -EFAULT;
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int k3dh_suspend(struct device *dev)
{
	int res = 0;
	struct k3dh_data *k3dh = dev_get_drvdata(dev);

	if (atomic_read(&k3dh->opened) > 0)
		res = i2c_smbus_write_byte_data(k3dh->client,
						CTRL_REG1, PM_OFF);
	if(res) {
		pr_err("%s : failed to CTRL_REG1 res=%d\n", __func__, res);
	}
	if(k3dh->power_off)
		k3dh->power_off();

#if defined (CONFIG_KOR_MODEL_SHV_E120L)||defined (CONFIG_KOR_MODEL_SHV_E120S)||defined(CONFIG_KOR_MODEL_SHV_E120K)
	if(gpio_free_count != 0){
		 gpio_direction_input(SENSE_SCL);
		 gpio_direction_input(SENSE_SDA);
		 gpio_free(SENSE_SCL);
		 gpio_free(SENSE_SDA);
		 gpio_free_count = 0;
		}
	else{
		gpio_free_count = 1;
		}
#endif

	return 0;
}

static int k3dh_resume(struct device *dev)
{
	int res = 0;
	struct k3dh_data *k3dh = dev_get_drvdata(dev);

#if defined (CONFIG_KOR_MODEL_SHV_E120L)||defined (CONFIG_KOR_MODEL_SHV_E120S)||defined(CONFIG_KOR_MODEL_SHV_E120K)
	if(gpio_request_count == 0){
		gpio_request(SENSE_SCL,"SENSE_SCL");
		gpio_request(SENSE_SDA,"SENSE_SDA");
		gpio_request_count =1;
		}
	else{
		gpio_request_count = 0;
		}
#endif


	if(k3dh->power_on)
		k3dh->power_on();

	if (atomic_read(&k3dh->opened) > 0) {
		res = i2c_smbus_write_byte_data(k3dh->client, CTRL_REG1, k3dh->ctrl_reg1_shadow);
	} else {
        k3dh->ctrl_reg1_shadow = DEFAULT_POWER_ON_SETTING;
        res = i2c_smbus_write_byte_data(k3dh->client, CTRL_REG1, DEFAULT_POWER_ON_SETTING);
	}

	if(res) {
		pr_err("[ACC] %s : failed to CTRL_REG1 res=%d\n", __func__, res);
	}

	res = i2c_smbus_write_byte_data(k3dh->client, CTRL_REG4, CTRL_REG4_HR);

	if (res) {
		pr_err("[ACC] %s: i2c write ctrl_reg4 failed res=%d\n", __func__, res);
	}

	return 0;
}

static const struct dev_pm_ops k3dh_pm_ops = {
	.suspend = k3dh_suspend,
	.resume = k3dh_resume,
};

static const struct file_operations k3dh_fops = {
	.owner = THIS_MODULE,
	.open = k3dh_open,
	.release = k3dh_close,
	.unlocked_ioctl = k3dh_ioctl,
};

static ssize_t k3dh_fs_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct k3dh_data *k3dh = dev_get_drvdata(dev);
	struct k3dh_acc data = { 0, };
	int err = 0;
	int on;

	mutex_lock(&k3dh->write_lock);
	on = atomic_read(&k3dh->opened);
	if (on == 0) {
		err = i2c_smbus_write_byte_data(k3dh->client, CTRL_REG1, DEFAULT_POWER_ON_SETTING);
	}
	mutex_unlock(&k3dh->write_lock);

	if (err < 0) {
			pr_err("%s: i2c write ctrl_reg1 failed\n", __func__);
			return err;
		}

	err = k3dh_read_accel_xyz(k3dh, &data);
	if (err < 0) {
		pr_err("%s: k3dh_read_accel_xyz failed\n", __func__);
		return err;
	}

	if (on == 0) {
		mutex_lock(&k3dh->write_lock);
		err = i2c_smbus_write_byte_data(k3dh->client, CTRL_REG1, PM_OFF);
		mutex_unlock(&k3dh->write_lock);
		if (err) {
			pr_err("%s: i2c write ctrl_reg1 failed\n", __func__);
		}
	}
	return sprintf(buf, "%d,%d,%d\n", data.x, data.y, data.z);
}

static ssize_t hwrev_read_for_sensor(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", get_hw_rev());
}

static ssize_t k3dh_calibration_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int err;
	struct k3dh_data *k3dh = dev_get_drvdata(dev);

	err = k3dh_open_calibration(k3dh);
	if (err < 0)
		pr_err("[ACC] %s: k3dh_open_calibration() failed\n", __func__);

	return sprintf(buf, "%d %d %d\n", k3dh->cal_data.x, k3dh->cal_data.y, k3dh->cal_data.z);
}

static ssize_t k3dh_calibration_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	// struct k3dh_data *k3dh = dev_get_drvdata(dev);
	int err;
	int data_buf = 2;
	// struct file *cal_filp = NULL;
	// mm_segment_t old_fs;

	sscanf(buf, "%d", &data_buf);

	if(data_buf == 1) {
		err = k3dh_do_calibrate(dev, 0); // do calibration 
		if (err < 0)
			pr_err("%s: k3dh_do_calibrate() failed\n", __func__);
	}
	else if(data_buf == 0) {
		err = k3dh_do_calibrate(dev, 1); // initialize calibration data
		if (err < 0)
			pr_err("%s: k3dh initialze calibration data failed\n", __func__);
	}
	else {
		pr_err("%s: buffer prameter error \n", __func__);
	}

	return count;
}

static DEVICE_ATTR(calibration, 0664,
		   k3dh_calibration_show, k3dh_calibration_store);
static DEVICE_ATTR(acc_file, 0664, k3dh_fs_read, NULL);

static DEVICE_ATTR(acc_hwrev, 0664, hwrev_read_for_sensor, NULL);

static int k3dh_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct k3dh_data *k3dh;
	struct device *dev_t, *dev_cal;
	struct k3dh_platform_data *pdata = client->dev.platform_data;
	int err;

	printk("k3dh probe start!!\n");

	if (!pdata) {
		pr_err("%s: missing pdata!\n", __func__);
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA |
				     I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
		pr_err("%s: i2c functionality check failed!\n", __func__);
		err = -ENODEV;
		goto exit;
	}

	k3dh = kzalloc(sizeof(struct k3dh_data), GFP_KERNEL);
	if (k3dh == NULL) {
		dev_err(&client->dev,
				"failed to allocate memory for module data\n");
		err = -ENOMEM;
		goto exit;
	}

	k3dh->client = client;
	k3dh->pdata = pdata;
	i2c_set_clientdata(client, k3dh);

	if(pdata->power_on)
		k3dh->power_on = pdata->power_on;
	if(pdata->power_off)
		k3dh->power_off = pdata->power_off;

	if(k3dh->power_on)
		k3dh->power_on();

	init_completion(&k3dh->data_ready);
	mutex_init(&k3dh->read_lock);
	mutex_init(&k3dh->write_lock);
	atomic_set(&k3dh->opened, 0);

	/* sensor HAL expects to find /dev/accelerometer */
	k3dh->k3dh_device.minor = MISC_DYNAMIC_MINOR;
	k3dh->k3dh_device.name = "accelerometer";
	k3dh->k3dh_device.fops = &k3dh_fops;
	err = misc_register(&k3dh->k3dh_device);
	if (err) {
		pr_err("%s: misc_register failed\n", __FILE__);
		goto err_misc_register;
	}

	/* creating class/device for test */
	k3dh->acc_class = class_create(THIS_MODULE, "accelerometer");
	if (IS_ERR(k3dh->acc_class)) {
		pr_err("%s: class create failed(accelerometer)\n", __func__);
		err = PTR_ERR(k3dh->acc_class);
		goto err_class_create;
	}

	dev_t = device_create(k3dh->acc_class, NULL,
				MKDEV(ACC_DEV_MAJOR, 0), "%s", "accelerometer");
	if (IS_ERR(dev_t)) {
		pr_err("%s: device create failed(accelerometer)\n", __func__);
		err = PTR_ERR(dev_t);
		goto err_acc_device_create;
	}

	err = device_create_file(dev_t, &dev_attr_acc_file);
	if (err < 0) {
		pr_err("%s: Failed to create device file(%s)\n",
				__func__, dev_attr_acc_file.attr.name);
		goto err_acc_device_create_file;
	}

	err = device_create_file(dev_t, &dev_attr_acc_hwrev);
	if (err < 0) {
		pr_err("%s: Failed to create device file2(%s)\n",
				__func__, dev_attr_acc_hwrev.attr.name);
		goto err_acc_device_create_file;  
	}
	
	dev_set_drvdata(dev_t, k3dh);

	/* creating device for calibration */
	dev_cal = device_create(sec_class, NULL, 0, NULL, "gsensorcal");
	if (IS_ERR(dev_cal)) {
		pr_err("%s: class create failed(gsensorcal)\n", __func__);
		err = PTR_ERR(dev_cal);
		goto err_cal_device_create;
	}

	err = device_create_file(dev_cal, &dev_attr_calibration);
	if (err < 0) {
		pr_err("%s: Failed to create device file(%s)\n",
				__func__, dev_attr_calibration.attr.name);
		goto err_cal_device_create_file;
	}
	dev_set_drvdata(dev_cal, k3dh);

	printk("k3dh probe success!!\n");

	return 0;

err_cal_device_create_file:
	device_destroy(sec_class, 0);
err_cal_device_create:
	device_remove_file(dev_t, &dev_attr_acc_file);
	device_remove_file(dev_t, &dev_attr_acc_hwrev);
err_acc_device_create_file:
	device_destroy(k3dh->acc_class, MKDEV(ACC_DEV_MAJOR, 0));
err_acc_device_create:
	class_destroy(k3dh->acc_class);
err_class_create:
	misc_deregister(&k3dh->k3dh_device);
err_misc_register:
	mutex_destroy(&k3dh->read_lock);
	mutex_destroy(&k3dh->write_lock);
	kfree(k3dh);
exit:
	return err;
}

static int k3dh_remove(struct i2c_client *client)
{
	struct k3dh_data *k3dh = i2c_get_clientdata(client);

	device_destroy(sec_class, 0);
	device_destroy(k3dh->acc_class, MKDEV(ACC_DEV_MAJOR, 0));
	class_destroy(k3dh->acc_class);
	misc_deregister(&k3dh->k3dh_device);
	mutex_destroy(&k3dh->read_lock);
	mutex_destroy(&k3dh->write_lock);
	kfree(k3dh);

	return 0;
}

static const struct i2c_device_id k3dh_id[] = {
	{ "k3dh", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, k3dh_id);

static struct i2c_driver k3dh_driver = {
	.probe = k3dh_probe,
	.remove = __devexit_p(k3dh_remove),
	.id_table = k3dh_id,
	.driver = {
		.pm = &k3dh_pm_ops,
		.owner = THIS_MODULE,
		.name = "k3dh",
	},
};

#ifdef CONFIG_BATTERY_SEC
extern unsigned int is_lpcharging_state(void);
#endif

static int __init k3dh_init(void)
{
#ifdef CONFIG_BATTERY_SEC
	if (is_lpcharging_state()) {
		pr_info("%s : LPM Charging Mode! return 0\n", __func__);
		return 0;
	}
#endif	

	return i2c_add_driver(&k3dh_driver);
}

static void __exit k3dh_exit(void)
{
	i2c_del_driver(&k3dh_driver);
}

module_init(k3dh_init);
module_exit(k3dh_exit);

MODULE_DESCRIPTION("k3dh accelerometer driver");
MODULE_AUTHOR("tim.sk.lee@samsung.com");
MODULE_LICENSE("GPL");
