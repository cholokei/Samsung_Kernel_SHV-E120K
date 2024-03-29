/*
** =========================================================================
** File:
**     tspdrv.c
**
** Description: 
**     TouchSense Kernel Module main entry-point.
**
** Portions Copyright (c) 2008-2010 Immersion Corporation. All Rights Reserved.
**
** This file contains Original Code and/or Modifications of Original Code 
** as defined in and that are subject to the GNU Public License v2 - 
** (the 'License'). You may not use this file except in compliance with the 
** License. You should have received a copy of the GNU General Public License 
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or contact 
** TouchSenseSales@immersion.com.
**
** The Original Code and all software distributed under the License are 
** distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER 
** EXPRESS OR IMPLIED, AND IMMERSION HEREBY DISCLAIMS ALL SUCH WARRANTIES, 
** INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS 
** FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. Please see 
** the License for the specific language governing rights and limitations 
** under the License.
** =========================================================================
*/

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/hrtimer.h>
#include "../../staging/android/timed_output.h"
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/wakelock.h>
#include <linux/io.h>
#include "tspdrv.h"
#include "ImmVibeSPI.c"
#include <linux/i2c.h>
#include <linux/earlysuspend.h>
#if defined(VIBE_DEBUG) && defined(VIBE_RECORD)
#include <tspdrvRecorder.c>
#endif

#if defined(CONFIG_USA_MODEL_SGH_T989D) || defined(CONFIG_KOR_MODEL_SHV_E160S) || defined(CONFIG_KOR_MODEL_SHV_E160K) ||defined(CONFIG_KOR_MODEL_SHV_E160L)
#define VIBE_MINOR 250
#endif
/* Device name and version information */

/* DO NOT CHANGE - this is auto-generated */
#define VERSION_STR " v3.4.55.8\n"

/* account extra space for future extra digits in version number */
#define VERSION_STR_LEN 16

/* initialized in init_module */
static char g_szDeviceName[  (VIBE_MAX_DEVICE_NAME_LENGTH 
                            + VERSION_STR_LEN)
							* NUM_ACTUATORS];
/* initialized in init_module */
static size_t g_cchDeviceName;

//static struct wake_lock vib_wake_lock;
static uint32_t vibrator_device_gpio_config[] = {
	GPIO_CFG(30, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),
	GPIO_CFG(31, 2, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
};
static struct wake_lock vib_wake_lock;

/* Flag indicating whether the driver is in use */
static char g_bIsPlaying;

/* Buffer to store data sent to SPI */
#define SPI_BUFFER_SIZE (NUM_ACTUATORS * \
	(VIBE_OUTPUT_SAMPLE_SIZE + SPI_HEADER_SIZE))
static int g_bStopRequested;
static actuator_samples_buffer g_SamplesBuffer[NUM_ACTUATORS] = {{0}}; 
static char g_cWriteBuffer[SPI_BUFFER_SIZE];

/* #define VIBE_TUNING */
/* #define VIBE_ENABLE_SYSTEM_TIMER */
/* #define IMPLEMENT_AS_CHAR_DRIVER */

/* For QA purposes */
#ifdef QA_TEST
#define FORCE_LOG_BUFFER_SIZE   128
#define TIME_INCREMENT          5
static int g_nTime;
static int g_nForceLogIndex;
static VibeInt8 g_nForceLog[FORCE_LOG_BUFFER_SIZE];
#endif

#if ((LINUX_VERSION_CODE & 0xFFFF00) < KERNEL_VERSION(2,6,0))
#error Unsupported Kernel version
#endif

/* Needs to be included after the global variables because it uses them */
#ifdef CONFIG_HIGH_RES_TIMERS
#include "VibeOSKernelLinuxHRTime.c"
#else
#include "VibeOSKernelLinuxTime.c"
#endif


/* timed_output */
#define VIBRATOR_PERIOD	87084/2
#define VIBRATOR_DUTY	87000/2

static struct hrtimer timer;

static int max_timeout = 5000;
static int vibrator_value = 0;
static int vibrator_work;
static int isa1200_enabled;


struct pwm_device	*vib_pwm;
extern unsigned int  get_hw_rev(void);
static void _set_vibetonz_work(struct work_struct *unused);

static DECLARE_WORK(vibetonz_work, _set_vibetonz_work);


static int set_vibetonz(int timeout)
{
	if(!timeout) {
		printk("[VIBETONZ] DISABLE\n");
#if defined (CONFIG_KOR_MODEL_SHV_E110S)		
		if (get_hw_rev() > 0x00 ){
			vib_isa1200_onoff(0);	
			vibtonz_en(0);
		} else {
			gpio_set_value(VIB_EN, VIBRATION_OFF);	
		}		
#elif defined (CONFIG_KOR_SHV_E120L_HD720) || defined(CONFIG_KOR_MODEL_SHV_E120K) || defined(CONFIG_KOR_MODEL_SHV_E120S) || defined(CONFIG_KOR_MODEL_SHV_E160S) || defined(CONFIG_KOR_MODEL_SHV_E160K) || defined(CONFIG_KOR_MODEL_SHV_E160L) ||  defined (CONFIG_KOR_MODEL_SHV_E120L)
		vib_isa1200_onoff(0);	
		vibtonz_en(0);
#elif defined (CONFIG_USA_MODEL_SGH_T989)|| defined (CONFIG_USA_MODEL_SGH_I727) || defined (CONFIG_USA_MODEL_SGH_T769)
		if (get_hw_rev() > 0x04 ){
			vib_isa1200_onoff(0);	
			vibtonz_en(0);
		} else {
			gpio_set_value(VIB_EN, VIBRATION_OFF);	
		}		
#elif defined (CONFIG_USA_MODEL_SGH_I717) || defined(CONFIG_USA_MODEL_SGH_I757) || defined(CONFIG_USA_MODEL_SGH_I577)

        vib_isa1200_onoff(0);
        vibtonz_en(0);
        
#elif defined (CONFIG_JPN_MODEL_SC_03D)
		if (get_hw_rev() > 0x00 ){
			vib_isa1200_onoff(0);	
			vibtonz_en(0);
		} else {
			gpio_set_value(VIB_EN, VIBRATION_OFF);	
		}	
#else	
		gpio_set_value(VIB_EN, VIBRATION_OFF);	
#endif		
	} else {
		printk("[VIBETONZ] ENABLE\n");
#if defined (CONFIG_KOR_MODEL_SHV_E110S)		
		if (get_hw_rev() > 0x00 ){
			vibtonz_en(1);
			vibe_set_pwm_freq(258);
			vib_isa1200_onoff(1);							
		} else {
			gpio_set_value(VIB_EN, VIBRATION_ON);			
		}
#elif defined (CONFIG_KOR_SHV_E120L_HD720) || defined(CONFIG_KOR_MODEL_SHV_E120K) || defined(CONFIG_KOR_MODEL_SHV_E120S) || defined(CONFIG_KOR_MODEL_SHV_E160S) || defined(CONFIG_KOR_MODEL_SHV_E160K) || defined(CONFIG_KOR_MODEL_SHV_E160L) ||  defined (CONFIG_KOR_MODEL_SHV_E120L)
		vibtonz_en(1);
		vibe_set_pwm_freq(258);
		vib_isa1200_onoff(1);							
#elif defined (CONFIG_USA_MODEL_SGH_T989)|| defined (CONFIG_USA_MODEL_SGH_I727) || defined (CONFIG_USA_MODEL_SGH_T769)
		if (get_hw_rev() > 0x04 ){
			vibtonz_en(1);
			vibe_set_pwm_freq(258);
			vib_isa1200_onoff(1);	
		} else {
			gpio_set_value(VIB_EN, VIBRATION_ON);			
		}
#elif defined (CONFIG_USA_MODEL_SGH_I717) || defined(CONFIG_USA_MODEL_SGH_I757) || defined(CONFIG_USA_MODEL_SGH_I577)

        vibtonz_en(1);
        vibe_set_pwm_freq(258);
        vib_isa1200_onoff(1);   


#elif defined (CONFIG_JPN_MODEL_SC_03D)
		if (get_hw_rev() > 0x00 ){
			vibtonz_en(1);
			vibe_set_pwm_freq(258);
			vib_isa1200_onoff(1);							
		} else {
			gpio_set_value(VIB_EN, VIBRATION_ON);			
		}	
#else			
		gpio_set_value(VIB_EN, VIBRATION_ON);		
#endif
	}

	vibrator_value = timeout;
	
	return 0;
}

static void _set_vibetonz_work(struct work_struct *unused)
{
	set_vibetonz(vibrator_work);

	return;
}


static enum hrtimer_restart vibetonz_timer_func(struct hrtimer *timer)
{
	/* set_vibetonz(0); */
	vibrator_work = 0;
	schedule_work(&vibetonz_work);

	return HRTIMER_NORESTART;
}

static int get_time_for_vibetonz(struct timed_output_dev *dev)
{
	int remaining;

	if (hrtimer_active(&timer)) {
		ktime_t r = hrtimer_get_remaining(&timer);
		remaining = r.tv.sec * 1000 + r.tv.nsec / 1000000;
	} else
		remaining = 0;

	if (vibrator_value ==-1)
		remaining = -1;

	return remaining;

}

static void enable_vibetonz_from_user(struct timed_output_dev *dev,int value)
{
	printk("[VIBETONZ] %s : time = %d msec \n",__func__,value);
	hrtimer_cancel(&timer);
	
	/* set_vibetonz(value); */
	vibrator_work = value;
	schedule_work(&vibetonz_work);


	if (value > 0) 
	{
		if (value > max_timeout)
			value = max_timeout;

		hrtimer_start(&timer,
						ktime_set(value / 1000, (value % 1000) * 1000000),
						HRTIMER_MODE_REL);
		vibrator_value = 0;
	}
}

static struct timed_output_dev timed_output_vt = {
	.name     = "vibrator",
	.get_time = get_time_for_vibetonz,
	.enable   = enable_vibetonz_from_user,
};

static void vibetonz_start(void)
{
	int ret = 0;

	hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer.function = vibetonz_timer_func;

	ret = timed_output_dev_register(&timed_output_vt);
	if(ret)
		printk(KERN_ERR "[VIBETONZ] timed_output_dev_register is fail \n");	
}


/* File IO */
static int open(struct inode *inode, struct file *file);
static int release(struct inode *inode, struct file *file);
static ssize_t read(struct file *file, char *buf, size_t count, loff_t *ppos);
static ssize_t write(struct file *file, const char *buf, size_t count,
	loff_t *ppos);
static long unlocked_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg);
static const struct file_operations fops = {
    .owner =    THIS_MODULE,
    .read =     read,
    .write =    write,
    .unlocked_ioctl =   unlocked_ioctl,
    .open =     open,
    .release =          release,
	.llseek =	default_llseek
};

static struct miscdevice miscdev = {
#if defined(CONFIG_USA_MODEL_SGH_T989D) || defined(CONFIG_KOR_MODEL_SHV_E160S) || defined(CONFIG_KOR_MODEL_SHV_E160K) ||defined(CONFIG_KOR_MODEL_SHV_E160L)
	.minor = VIBE_MINOR,
#else
	.minor =    MISC_DYNAMIC_MINOR,
#endif			
	.name =     MODULE_NAME,
	.fops =     &fops
};

static int suspend(struct platform_device *pdev, pm_message_t state);
static int resume(struct platform_device *pdev);
static struct platform_driver platdrv = {
    .suspend =  suspend,	
    .resume =   resume,	
	.driver = {
        .name = MODULE_NAME,	
    },	
};

static void platform_release(struct device *dev);
static struct platform_device platdev = {
	.name =     MODULE_NAME,	
	/* means that there is only one device */
	.id =       -1,
	.dev = {
		.platform_data = NULL, 		
		/* a warning is thrown during rmmod if this is absent */
		.release = platform_release,
	},
};

/* Module info */
MODULE_AUTHOR("Immersion Corporation");
MODULE_DESCRIPTION("TouchSense Kernel Module");
MODULE_LICENSE("GPL v2");

static struct vibrator {
//	struct wake_lock wklock;
	struct hrtimer timer;
	struct mutex lock;
	struct work_struct work;
	struct clk *dap_mclk2;
	struct i2c_client *client;
	bool running;
} vibdata;

int vibrator_write_register(u8 addr, u8 w_data)
{
	if (i2c_smbus_write_byte_data(vibdata.client, addr, w_data) < 0) {
		pr_err("%s: Failed to write addr=[0x%x], data=[0x%x]\n",
		   __func__, addr, w_data);
		return -1;
	}

	return 0;
}

static int vibrator_i2c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	vibdata.client = client;

	pr_info("vibrator I2C attach success!!!\n");
	return 0;
}

static const struct i2c_device_id vibrator_device_id[] = {
	{"isa1200", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, vibrator_device_id);


static struct i2c_driver vibrator_i2c_driver = {
	.driver = {
		.name = "isa1200",
		.owner = THIS_MODULE,
	},
	.probe     = vibrator_i2c_probe,
	.id_table  = vibrator_device_id,
};

int init_module(void)
{
    int nRet, i;   /* initialized below */

    DbgOut((KERN_INFO "tspdrv: init_module.\n"));

    nRet = misc_register(&miscdev);
	printk("[VIBETONZ:WJYOO] MISC_REGISTER nRet = %d\n", nRet);
	if (nRet) 
    {
        DbgOut((KERN_ERR "tspdrv: misc_register failed.\n"));
		return nRet;
	}

	nRet = platform_device_register(&platdev);
	if (nRet) 
    {
        DbgOut((KERN_ERR "tspdrv: platform_device_register failed.\n"));
    }

	android_vib_clk = clk_get(NULL,"sfpb_clk");
	
	if(IS_ERR(android_vib_clk)) {
		printk("android vib clk failed!!!\n");
		} else {
		printk("THNAK YOU!!\n");
		}	

#if defined (CONFIG_KOR_MODEL_SHV_E110S)		
	if (get_hw_rev() > 0x00 ){	
		nRet = i2c_add_driver(&vibrator_i2c_driver);
		if (nRet)
		pr_err("%s: i2c_add_driver() failed err = %d\n", __func__, nRet);
	}
#elif defined (CONFIG_KOR_MODEL_SHV_E120L) || defined(CONFIG_KOR_MODEL_SHV_E120K) || defined(CONFIG_KOR_MODEL_SHV_E120S) || defined(CONFIG_KOR_MODEL_SHV_E160S) || defined(CONFIG_KOR_MODEL_SHV_E160K) || defined(CONFIG_KOR_MODEL_SHV_E160L)
	nRet = i2c_add_driver(&vibrator_i2c_driver);
	if (nRet)
	pr_err("%s: i2c_add_driver() failed err = %d\n", __func__, nRet);
#elif defined (CONFIG_USA_MODEL_SGH_T989)|| defined (CONFIG_USA_MODEL_SGH_I727) || defined (CONFIG_USA_MODEL_SGH_T769)
	if (get_hw_rev() > 0x04 ){	
		nRet = i2c_add_driver(&vibrator_i2c_driver);
		if (nRet)
		pr_err("%s: i2c_add_driver() failed err = %d\n", __func__, nRet);
		}
#elif defined (CONFIG_USA_MODEL_SGH_I717) || defined(CONFIG_USA_MODEL_SGH_I757) || defined(CONFIG_USA_MODEL_SGH_I577)

    nRet = i2c_add_driver(&vibrator_i2c_driver);

    if (nRet) {
        pr_err("%s: i2c_add_driver() failed err = %d\n", __func__, nRet);
    }    

#elif defined (CONFIG_JPN_MODEL_SC_03D)
		if (get_hw_rev() >= 0x01 ){ 
		nRet = i2c_add_driver(&vibrator_i2c_driver);
		if (nRet)
		pr_err("%s: i2c_add_driver() failed err = %d\n", __func__, nRet);
		}
#endif

    DbgRecorderInit(());

    ImmVibeSPI_ForceOut_Initialize();
    VibeOSKernelLinuxInitTimer();

	isa1200_enabled = 1;	
	platform_driver_register(&platdrv);

    /* Get and concatenate device name and initialize data buffer */
    g_cchDeviceName = 0;
	for (i = 0; i < NUM_ACTUATORS; i++) {
        char *szName = g_szDeviceName + g_cchDeviceName;
		ImmVibeSPI_Device_GetName(i, szName,
			VIBE_MAX_DEVICE_NAME_LENGTH);

        /* Append version information and get buffer length */
        strcat(szName, VERSION_STR);
        g_cchDeviceName += strlen(szName);

        g_SamplesBuffer[i].nIndexPlayingBuffer = -1; /* Not playing */
        g_SamplesBuffer[i].actuatorSamples[0].nBufferSize = 0;
        g_SamplesBuffer[i].actuatorSamples[1].nBufferSize = 0;
    }

    vibetonz_start();
    
//	wake_lock_init(&vib_wake_lock, WAKE_LOCK_SUSPEND, "vib_present");
    return 0;


//    return nRet;
}

void cleanup_module(void)
{
    DbgOut((KERN_INFO "tspdrv: cleanup_module.\n"));

    DbgRecorderTerminate(());

    VibeOSKernelLinuxTerminateTimer();
    ImmVibeSPI_ForceOut_Terminate();

	platform_driver_unregister(&platdrv);
	platform_device_unregister(&platdev);

    misc_deregister(&miscdev);

//	wake_lock_destroy(&vib_wake_lock);
    gpio_free(VIB_EN);
}

static int open(struct inode *inode, struct file *file) 
{
    DbgOut((KERN_INFO "tspdrv: open.\n"));

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

    return 0; 
}

static int release(struct inode *inode, struct file *file) 
{
    DbgOut((KERN_INFO "tspdrv: release.\n"));

    /* 
    ** Reset force and stop timer when the driver is closed, to make sure
    ** no dangling semaphore remains in the system, especially when the
    ** driver is run outside of immvibed for testing purposes.
    */
    VibeOSKernelLinuxStopTimer();

    /* 
    ** Clear the variable used to store the magic number to prevent 
    ** unauthorized caller to write data. TouchSense service is the only 
    ** valid caller.
    */
    file->private_data = (void*)NULL;

    module_put(THIS_MODULE);

    return 0; 
}

static ssize_t read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	const size_t nBufSize = (g_cchDeviceName > (size_t)(*ppos)) ?
		min(count, g_cchDeviceName - (size_t)(*ppos)) : 0;

    /* End of buffer, exit */
	if (0 == nBufSize)
		return 0;

	if (0 != copy_to_user(buf, g_szDeviceName + (*ppos), nBufSize))	{
        /* Failed to copy all the data, exit */
        DbgOut((KERN_ERR "tspdrv: copy_to_user failed.\n"));
        return 0;
    }

    /* Update file position and return copied buffer size */
    *ppos += nBufSize;
    return nBufSize;
}

static ssize_t write(struct file *file, const char *buf, size_t count,
	loff_t *ppos)
{
    int i = 0;

    *ppos = 0;  /* file position not used, always set to 0 */

    /* 
    ** Prevent unauthorized caller to write data. 
    ** TouchSense service is the only valid caller.
    */
	if (file->private_data != (void *)TSPDRV_MAGIC_NUMBER) {
        DbgOut((KERN_ERR "tspdrv: unauthorized write.\n"));
        return 0;
    }

    /* Copy immediately the input buffer */
	if (0 != copy_from_user(g_cWriteBuffer, buf, count)) {
        /* Failed to copy all the data, exit */
        DbgOut((KERN_ERR "tspdrv: copy_from_user failed.\n"));
        return 0;
    }

    /* Check buffer size */
	if ((count <= SPI_HEADER_SIZE) || (count > SPI_BUFFER_SIZE)) {
        DbgOut((KERN_ERR "tspdrv: invalid write buffer size.\n"));
        return 0;
    }

	while (i < count) {
        int nIndexFreeBuffer;   /* initialized below */

		samples_buffer* pInputBuffer =	(samples_buffer *)
			(&g_cWriteBuffer[i]);

		if ((i + SPI_HEADER_SIZE) >= count) {
            /*
            ** Index is about to go beyond the buffer size.
            ** (Should never happen).
            */
            DbgOut((KERN_EMERG "tspdrv: invalid buffer index.\n"));
        }

        /* Check bit depth */
        if (8 != pInputBuffer->nBitDepth)
			DbgOut((KERN_WARNING
				"tspdrv: invalid bit depth."
				"Use default value (8)\n"));

        /* The above code not valid if SPI header size is not 3 */
#if (SPI_HEADER_SIZE != 3)
#error "SPI_HEADER_SIZE expected to be 3"
#endif

        /* Check buffer size */
		if ((i + SPI_HEADER_SIZE + pInputBuffer->nBufferSize) > count) {
            /*
            ** Index is about to go beyond the buffer size.
            ** (Should never happen).
            */
            DbgOut((KERN_EMERG "tspdrv: invalid data size.\n"));
        }
        
        /* Check actuator index */
		if (NUM_ACTUATORS <= pInputBuffer->nActuatorIndex) {
            DbgOut((KERN_ERR "tspdrv: invalid actuator index.\n"));
            i += (SPI_HEADER_SIZE + pInputBuffer->nBufferSize);
            continue;
        }

		if (0 == g_SamplesBuffer[pInputBuffer->nActuatorIndex].
			actuatorSamples[0].nBufferSize) {
            nIndexFreeBuffer = 0;
		} else if (0 == g_SamplesBuffer[pInputBuffer->nActuatorIndex].
			actuatorSamples[1].nBufferSize) {
             nIndexFreeBuffer = 1;
		} else {
            /* No room to store new samples  */
			DbgOut((KERN_ERR
				"tspdrv: no room to store new samples.\n"));
            return 0;
        }

        /* Store the data in the free buffer of the given actuator */
		memcpy(&(g_SamplesBuffer[pInputBuffer->nActuatorIndex].
			actuatorSamples[nIndexFreeBuffer]), &g_cWriteBuffer[i],
			(SPI_HEADER_SIZE + pInputBuffer->nBufferSize));

		/* if the no buffer is playing, prepare to play
		  * g_SamplesBuffer[pInputBuffer->nActuatorIndex].
		  * actuatorSamples[nIndexFreeBuffer] */
		if (-1 == g_SamplesBuffer[pInputBuffer->
			nActuatorIndex].nIndexPlayingBuffer) {
			g_SamplesBuffer[pInputBuffer->nActuatorIndex].
				nIndexPlayingBuffer = nIndexFreeBuffer;
			g_SamplesBuffer[pInputBuffer->nActuatorIndex].
				nIndexOutputValue = 0;
        }

        /* Call SPI */
       ImmVibeSPI_ForceOut_SetSamples(pInputBuffer->nActuatorIndex, pInputBuffer->nBitDepth, pInputBuffer->nBufferSize, &(g_SamplesBuffer[pInputBuffer->nActuatorIndex].actuatorSamples[nIndexFreeBuffer].dataBuffer[0]));

        /* Increment buffer index */
        i += (SPI_HEADER_SIZE + pInputBuffer->nBufferSize);
    }

#ifdef QA_TEST
    g_nForceLog[g_nForceLogIndex++] = g_cSPIBuffer[0];
	if (g_nForceLogIndex >= FORCE_LOG_BUFFER_SIZE) {
		for (i = 0; i < FORCE_LOG_BUFFER_SIZE; i++) {
			printk(KERN_DEBUG "<6>%d\t%d\n",
				g_nTime, g_nForceLog[i]);
            g_nTime += TIME_INCREMENT;
        }
        g_nForceLogIndex = 0;
    }
#endif

    /* Start the timer after receiving new output force */
    g_bIsPlaying = true;
    VibeOSKernelLinuxStartTimer();

    return count;
}

static long unlocked_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
#ifdef QA_TEST
    int i;
#endif

	DbgOut((KERN_INFO "tspdrv: ioctl cmd[0x%x].\n", cmd));

	switch (cmd) {
        case TSPDRV_STOP_KERNEL_TIMER:
            /* 
		  * As we send one sample ahead of time,
		  * we need to finish playing the last sample
		  * before stopping the timer. So we just set a flag here.
            */
		if (true == g_bIsPlaying)
			g_bStopRequested = true;

#ifdef VIBEOSKERNELPROCESSDATA
            /* Last data processing to disable amp and stop timer */
            VibeOSKernelProcessData(NULL);
#endif

#ifdef QA_TEST
		if (g_nForceLogIndex) {
			for (i = 0; i < g_nForceLogIndex; i++) {
				printk(KERN_DEBUG "<6>%d\t%d\n",
					g_nTime, g_nForceLog[i]);
                    g_nTime += TIME_INCREMENT;
                }
            }
            g_nTime = 0;
            g_nForceLogIndex = 0;
#endif
            break;

        case TSPDRV_MAGIC_NUMBER:
            file->private_data = (void*)TSPDRV_MAGIC_NUMBER;
            break;

        case TSPDRV_ENABLE_AMP:
		//wake_lock(&vib_wake_lock);
            ImmVibeSPI_ForceOut_AmpEnable(arg);
            DbgRecorderReset((arg));
            DbgRecord((arg,";------- TSPDRV_ENABLE_AMP ---------\n"));
            break;

        case TSPDRV_DISABLE_AMP:
		/* Small fix for now to handle proper combination of
		  * TSPDRV_STOP_KERNEL_TIMER and TSPDRV_DISABLE_AMP together
		  * If a stop was requested, ignore the request as the amp
		  * will be disabled by the timer proc when it's ready
		  */
            if(!g_bStopRequested)
            ImmVibeSPI_ForceOut_AmpDisable(arg);
		//wake_unlock(&vib_wake_lock);
            break;

        case TSPDRV_GET_NUM_ACTUATORS:
            return NUM_ACTUATORS;
    }

    return 0;
}

static int suspend(struct platform_device *pdev, pm_message_t state) 
{
	int ret;

    if (g_bIsPlaying)
    {
	ret = -EBUSY;
    }
    else
    {
        DbgOut((KERN_INFO "tspdrv: suspend.\n"));
#if defined (CONFIG_KOR_MODEL_SHV_E110S)
		if (get_hw_rev() > 0x00){
			if(isa1200_enabled){
				vibrator_write_register(0x30, 0x09);
				gpio_set_value(VIB_EN, VIBRATION_OFF);				
				isa1200_enabled = 0;	
			}
	
			printk(KERN_ERR "[VIBTONZ] isa1200_early_suspend \n");
		}
#elif defined (CONFIG_KOR_SHV_E120L_HD720) || defined(CONFIG_KOR_MODEL_SHV_E120K) || defined(CONFIG_KOR_MODEL_SHV_E120S) || defined(CONFIG_KOR_MODEL_SHV_E160S) || defined(CONFIG_KOR_MODEL_SHV_E160K) || defined(CONFIG_KOR_MODEL_SHV_E160L) ||  defined (CONFIG_KOR_MODEL_SHV_E120L)
		if(isa1200_enabled){
			vibrator_write_register(0x30, 0x09);
			gpio_set_value(VIB_EN, VIBRATION_OFF);				
			isa1200_enabled = 0;	
		}
		printk(KERN_ERR "[VIBTONZ] isa1200_early_suspend \n");
#elif defined (CONFIG_USA_MODEL_SGH_T989)|| defined (CONFIG_USA_MODEL_SGH_I727) || defined (CONFIG_USA_MODEL_SGH_T769)
		if (get_hw_rev() > 0x04){
			if(isa1200_enabled){
				vibrator_write_register(0x30, 0x09);
				gpio_set_value(VIB_EN, VIBRATION_OFF);				
				isa1200_enabled = 0;	
			}
	
			printk(KERN_ERR "[VIBTONZ] isa1200_early_suspend \n");
		}
#elif defined (CONFIG_USA_MODEL_SGH_I717) || defined(CONFIG_USA_MODEL_SGH_I757) || defined(CONFIG_USA_MODEL_SGH_I577)

        if(isa1200_enabled){
            vibrator_write_register(0x30, 0x09);
            gpio_set_value(VIB_EN, VIBRATION_OFF);              
            isa1200_enabled = 0;    
        }
        
        printk(KERN_ERR "[VIBTONZ] isa1200_early_suspend \n");

        
#elif defined (CONFIG_JPN_MODEL_SC_03D)
		if (get_hw_rev() > 0x00){
			if(isa1200_enabled){
				vibrator_write_register(0x30, 0x09);
				gpio_set_value(VIB_EN, VIBRATION_OFF);				
				isa1200_enabled = 0;	
			}

			printk(KERN_ERR "[VIBTONZ] isa1200_early_suspend \n");
		}
#endif			

    }

    return 0;
}

static int resume(struct platform_device *pdev) 
{	
    DbgOut((KERN_INFO "tspdrv: resume.\n"));
	
#if defined (CONFIG_KOR_MODEL_SHV_E110S)
		if (get_hw_rev() > 0x00){	
			if(!isa1200_enabled){
				gpio_set_value(VIB_EN, VIBRATION_ON);		
				vibrator_write_register(0x30, 0x09);
				vibrator_write_register(0x31, 0x40);
				vibrator_write_register(0x34, 0x19);
				vibrator_write_register(0x35, 0x00);	
				vibrator_write_register(0x36, 0x00);
	
				isa1200_enabled = 1;				
			}
	
			printk(KERN_ERR "[VIBTONZ] isa1200_late_resume \n");
		}
#elif defined (CONFIG_KOR_SHV_E120L_HD720) || defined(CONFIG_KOR_MODEL_SHV_E120S) || defined(CONFIG_KOR_MODEL_SHV_E120K) ||  defined (CONFIG_KOR_MODEL_SHV_E120L)
		if(!isa1200_enabled){
			gpio_set_value(VIB_EN, VIBRATION_ON);		
			vibrator_write_register(0x30, 0x09);
			vibrator_write_register(0x31, 0x40);
			vibrator_write_register(0x32, 0x00);
			vibrator_write_register(0x33, 0x13);
			vibrator_write_register(0x34, 0x05);
			vibrator_write_register(0x35, 0x00);
			vibrator_write_register(0x36, 0x00);
			isa1200_enabled = 1;				
		}

		printk(KERN_ERR "[VIBTONZ] isa1200_late_resume \n");
#elif defined  (CONFIG_KOR_MODEL_SHV_E160L)
		if(!isa1200_enabled){
			gpio_set_value(VIB_EN, VIBRATION_ON);		
			vibrator_write_register(0x30, 0x09);
			vibrator_write_register(0x31, 0x40);
			vibrator_write_register(0x32, 0x00);
			vibrator_write_register(0x33, 0x13);
			vibrator_write_register(0x34, 0x02);
			vibrator_write_register(0x35, 0x00);
			vibrator_write_register(0x36, 0x00);
			isa1200_enabled = 1;				
		}

		printk(KERN_ERR "[VIBTONZ] isa1200_late_resume \n");
#elif defined(CONFIG_KOR_MODEL_SHV_E160S) || defined(CONFIG_KOR_MODEL_SHV_E160K)
		if(!isa1200_enabled){
			gpio_set_value(VIB_EN, VIBRATION_ON);		
			vibrator_write_register(0x30, 0x09);
			vibrator_write_register(0x31, 0x40);
			vibrator_write_register(0x32, 0x00);
			vibrator_write_register(0x33, 0x13);
			vibrator_write_register(0x34, 0x03);
			vibrator_write_register(0x35, 0x00);
			vibrator_write_register(0x36, 0x00);
			isa1200_enabled = 1;				
		}

		printk(KERN_ERR "[VIBTONZ] isa1200_late_resume \n");
#elif defined(CONFIG_USA_MODEL_SGH_I577) || defined(CONFIG_CAN_MODEL_SGH_I577R)
		if(!isa1200_enabled){
            gpio_set_value(VIB_EN, VIBRATION_ON);
            vibrator_write_register(0x30, 0x09);
            vibrator_write_register(0x31, 0x40);
            vibrator_write_register(0x34, 0x02);
            vibrator_write_register(0x35, 0x00);    
            vibrator_write_register(0x36, 0x00);
			isa1200_enabled = 1;				
		}

        printk(KERN_ERR "[VIBTONZ] isa1200_late_resume \n");        
        
#elif defined (CONFIG_USA_MODEL_SGH_T989)|| defined (CONFIG_USA_MODEL_SGH_I727) || defined (CONFIG_USA_MODEL_SGH_T769)
		if (get_hw_rev() > 0x04){	
			if(!isa1200_enabled){
				gpio_set_value(VIB_EN, VIBRATION_ON);		
				vibrator_write_register(0x30, 0x09);
				vibrator_write_register(0x31, 0x40);
				#if defined (CONFIG_USA_MODEL_SGH_T989)
				if (get_hw_rev() >= 0x0d)
					vibrator_write_register(0x34, 0x01);
				else				
					vibrator_write_register(0x34, 0x16);
				#elif defined (CONFIG_USA_MODEL_SGH_T769)
				    vibrator_write_register(0x34, 0x17);
				#else
				vibrator_write_register(0x34, 0x19);
				#endif
				vibrator_write_register(0x35, 0x00);	
				vibrator_write_register(0x36, 0x00);
	
				isa1200_enabled = 1;				
			}
	
			printk(KERN_ERR "[VIBTONZ] isa1200_late_resume \n");
		}

#elif defined (CONFIG_USA_MODEL_SGH_I717) || defined(CONFIG_USA_MODEL_SGH_I757)

		if(!isa1200_enabled){
            gpio_set_value(VIB_EN, VIBRATION_ON);       
            vibrator_write_register(0x30, 0x09);
            vibrator_write_register(0x31, 0x40);
            vibrator_write_register(0x34, 0x02);
            vibrator_write_register(0x35, 0x00);    
            vibrator_write_register(0x36, 0x00);
			isa1200_enabled = 1;				
		}

        printk(KERN_ERR "[VIBTONZ] isa1200_late_resume \n");

        
#elif defined (CONFIG_JPN_MODEL_SC_03D)
		if (get_hw_rev() > 0x00){	
			if(!isa1200_enabled){
				gpio_set_value(VIB_EN, VIBRATION_ON);		
				vibrator_write_register(0x30, 0x09);
				vibrator_write_register(0x31, 0x40);
				vibrator_write_register(0x34, 0x19);
				vibrator_write_register(0x35, 0x00);	
				vibrator_write_register(0x36, 0x00);

				isa1200_enabled = 1;				
			}

			printk(KERN_ERR "[VIBTONZ] isa1200_late_resume \n");
		}
#endif
	return 0;   /* can resume */
}

static void platform_release(struct device *dev) 
{	
    DbgOut((KERN_INFO "tspdrv: platform_release.\n"));
}
