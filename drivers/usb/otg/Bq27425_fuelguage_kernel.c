/*
 *  bq27425 fuelgauge
 *  fuel-gauge systems for lithium-ion (Li+) batteries
 *
 *  Copyright (C) 2010 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/pm.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/firmware.h>
#include <linux/wakelock.h>
#include <linux/blkdev.h>
#include <linux/workqueue.h>
#include <linux/rtc.h>
#include <mach/gpio.h>


#include <linux/delay.h>			//
#include <linux/module.h>		//
#include <linux/init.h>
#include <linux/platform_device.h>	//
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>			//
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/slab.h>			//
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/device.h>		//
#include <linux/gpio.h>

#include "../../../arch/arm/plat-omap/include/plat/i2c-omap-gpio.h"

//#include "bq27425.h"

// I2C Address
//#define TIbq27425_FIRM_SLAVE_ADDRESS		0xAA		/* 0110 110X: X= R/W */
//#define TIbq27425_ROM_SLAVE_ADDRESS		0x16		/* 0110 110X: X= R/W */

// TI Standard Commands in Firmware Mode
#define CNTL			0x00		//Control() 				N/A
#define TEMP			0x02		//Temperature()			0.1K
#define VOLT			0x04		//Voltage()				mV
#define FLAGS			0x06		//Flags()					N/A
#define NAC				0x08		//NominalAvailableCapacity()	mAh
#define FAC				0x0a		//FullAvailableCapacity()		mAh
#define RM				0x0c		//RemainingCapacity()		mAh
#define FCC				0x0e		//FullChargeCapacity()		mAh
#define AI				0x10		//AverageCurrent()			mA
#define SI				0x12		//StandbyCurrent()			mA
#define MLI				0x14		//MaxLoadCurrent()			mA
#define AP				0x18		//AveragePower()			mW
#define SOC				0x1c		//StateOfCharge()			%
#define ITEMP			0x1e		//IntTemperature()			0.1K
#define SOH				0x20		//StateOfHealth()			%


#define OPCODE_SET		0x3e
#define OPCODE_SET1		0x3f
#define	TEMP_SET		0x40

#define SAMSUNG_OPCODE						0xA1FC//0xA1F8


#define FUEL_I2C_SDA  61		// set gipo
#define FUEL_I2C_SCL  60

extern OMAP_GPIO_I2C_CLIENT * omap_gpio_i2c_init_fuel(const int sda, const int scl, const int addr, const int khz);
extern void omap_gpio_i2c_deinit_fuel(OMAP_GPIO_I2C_CLIENT * client);
extern int omap_gpio_i2c_write_fuel(OMAP_GPIO_I2C_CLIENT * client, OMAP_GPIO_I2C_WR_DATA *i2c_param);
extern int omap_gpio_i2c_read_fuel(OMAP_GPIO_I2C_CLIENT * client, OMAP_GPIO_I2C_RD_DATA *i2c_param);

//extern int check_jig_on(void);

static struct i2c_driver fg_i2c_driver;
static struct i2c_client *fg_i2c_client = NULL;

static int is_reset_soc = 0;

static int is_attached = 0;

static OMAP_GPIO_I2C_CLIENT * Bq27425_i2c_client;

struct fuel_gauge_data
{
	struct work_struct work;
};

static int bq27425_write_reg(struct i2c_client *client, int reg, u8 * buf)
{
	int ret;

#if 0
	ret = i2c_smbus_write_i2c_block_data(client, reg, 1, buf);
#else
	OMAP_GPIO_I2C_WR_DATA i2c_param;
	u8 reg_data[2] = {0};

	reg_data[0] = (u8)reg;
	i2c_param.reg_addr = reg_data;
	i2c_param.reg_len = 1;
	i2c_param.wdata = buf;
	i2c_param.wdata_len = 1;	// temprary

	ret = omap_gpio_i2c_write_fuel(Bq27425_i2c_client, &i2c_param);
#endif

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}



static int bq27425_read_reg(struct i2c_client *client, int reg, u8 * buf)
{
	int ret;

#if 0
	ret = i2c_smbus_read_i2c_block_data(client, reg, 2, buf);
#else
	OMAP_GPIO_I2C_RD_DATA i2c_param;
	u8 reg_data[2] = {0};

	reg_data[0] = (u8)reg;
	i2c_param.reg_addr = reg_data;
	i2c_param.reg_len = 1;
	i2c_param.rdata = buf;
	i2c_param.rdata_len = 2;	// temprary

	ret = omap_gpio_i2c_read_fuel(Bq27425_i2c_client, &i2c_param);
#endif

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}
static int bq27425_read32_reg(struct i2c_client *client, int reg, u8 * buf)
{
	int ret;
	int j;
	
	for(j=0; j< 8; j++){
		pr_info("[bq27425]OPCODE data kernel[%d] =0x%x \n",j,buf[j]);
	}	

#if 0
	ret = i2c_smbus_read_i2c_block_data(client, reg, 8, buf);
#else
	OMAP_GPIO_I2C_RD_DATA i2c_param;
	u8 reg_data[2] = {0};

	reg_data[0] = (u8)reg;
	i2c_param.reg_addr = reg_data;
	i2c_param.reg_len = 1;
	i2c_param.rdata = buf;
	i2c_param.rdata_len = 8;	// temprary

	ret = omap_gpio_i2c_read_fuel(Bq27425_i2c_client, &i2c_param);
#endif

	for(j=0; j< 8; j++){
		pr_info("[bq27425]OPCODE data kernel after [%d] =0x%x \n",j,buf[j]);
	}	

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}

/*
static void bq27425_write_reg_array(struct i2c_client *client,
				struct bq27425_reg_data *data, int size)
{
	int i;

	for (i = 0; i < size; i += 3)
		max17042_write_reg(client, (data + i)->reg_addr,
				   ((u8 *) (data + i)) + 1);
}*/

unsigned int bq27425_get_vcell(void)
{
	struct i2c_client *client = fg_i2c_client;

	u8 data[2];
	int volt;
	int count=0;
	static u8 sdata[2]= {0x68,0x10} ;

	

	
	#if 1
	
	do{
	bq27425_read_reg(client, VOLT, data);
	volt = ((data[0]) + (data[1]<<8));
	count ++;
	
	}
	while(volt>10000 && count<10);
		
	if (count ==10)
		{
		data[0] = sdata[0];
		data[1] = sdata[1];
	
		}
	else 
		{
		sdata[0] = data[0];
		sdata[1] = data[1];
		}
	#else
	if (bq27425_read_reg(client, RM, data) < 0)
	return -1;	
	#endif
	
	
	volt = ((data[0]) + (data[1]<<8));

	// add here  check batt is full or not
	return volt;
}

unsigned int bq27425_get_soc(void)
{
	struct i2c_client *client = fg_i2c_client;
	
	u8 data[2];
	int soc;
	#if 1

	do{
	bq27425_read_reg(client, SOC, data);
	soc = ((data[0]) + (data[1]<<8));
	}
	while(soc >10000);
	#else
	if (bq27425_read_reg(client, SOC, data) < 0)
	return -1;	
	#endif

	soc = ((data[0]) + (data[1]<<8));
       printk("d0 : %x  d1 : %x \n", data[0], data[1]);
	   
	// add here  check batt is full or not
	return soc;

}

unsigned int bq27425_get_FullChrgeCapacity(void)
{
	struct i2c_client *client = fg_i2c_client;
	
	u8 data[2];
	int fcc;
	int count=0;
	static u8 sdata[2] = { 0xdc, 0x05};

	
	#if 1
	
	do{
	bq27425_read_reg(client, FCC, data);
	fcc = ((data[0]) + (data[1]<<8));
	count ++;
	
	}
	while(fcc>10000 && count<10);
		
	if (count ==10 || fcc==0)
		{
		data[0] = sdata[0];
		data[1] = sdata[1];
	
		}
	else 
		{
		sdata[0] = data[0];
		sdata[1] = data[1];
		}
	#else
	if (bq27425_read_reg(client, RM, data) < 0)
	return -1;	
	#endif
	
	
	fcc = ((data[0]) + (data[1]<<8));

	// add here  check batt is full or not
	return fcc;

}


unsigned int bq27425_get_RemainingCapacity(void)
{
	struct i2c_client *client = fg_i2c_client;
	
	u8 data[2];
	int rm;
	int count=0;
	static u8 sdata[2]= {0xb0, 0x04};


	
	#if 1
	
	do{
	bq27425_read_reg(client, RM, data);
	rm = ((data[0]) + (data[1]<<8));
	count ++;
	
	}
	while(rm >10000 && count<10);
		
	if (count ==10)
		{
		data[0] = sdata[0];
		data[1] = sdata[1];
	
		}
	else 
		{
		sdata[0] = data[0];
		sdata[1] = data[1];
		}
	#else
	if (bq27425_read_reg(client, RM, data) < 0)
	return -1;	
	#endif
	
	
	rm = ((data[0]) + (data[1]<<8));

	// add here  check batt is full or not
	return rm;

}

unsigned int bq27425_get_FullAvailableCapacity(void)
{
	struct i2c_client *client = fg_i2c_client;
	
	u8 data[2];
	int fac;

	#if 1
	do{
	bq27425_read_reg(client, FAC, data);
	fac = ((data[0]) + (data[1]<<8));
	}
	while(fac >10000);
	#else
	if (bq27425_read_reg(client, FAC, data) < 0)
	return -1;	
	#endif
	
	
	fac = ((data[0]) + (data[1]<<8));

	// add here  check batt is full or not
	return fac;

}

unsigned int bq27425_get_AverageCurrent(void)
{
	struct i2c_client *client = fg_i2c_client;
	
	u8 data[2];
	int ai;
	int temp = 0;

	#if 1
	do{
	bq27425_read_reg(client, AI, data);

	if ((data[1] & 0x80))
		{
		ai = 0xffff - ((data[0]) + (data[1]<<8)) +1;
		temp = 0xffffffff - ai +1;
		ai =  temp;	
		}
	else
		{
		ai = ((data[0]) + (data[1]<<8));
		}
	//printk("[BQ27425] after ai : %d \n",ai);
	}
	while( 0xffff == ((data[0]) + (data[1]<<8)));
	#else
	if (bq27425_read_reg(client, FAC, data) < 0)
	return -1;	
	#endif
	//printk("d0 : %x  d1 : %x \n", data[0], data[1]);

	return ai;

}





static int bq27425_get_opcode(void)
{
	struct i2c_client *client = fg_i2c_client;

	pr_info("[BATT]insifffede opcode func \n");
	//u8 offset=0x52;
	u8 data[8]={0,};
	u8 raw_data[2];
	raw_data[0] = 0x52;
	raw_data[1] = 0x00;
	int ret ;

	bq27425_write_reg(client, OPCODE_SET, &raw_data[0]);
	pr_info("[BATT]read opcode func 1 \n");
	//offset=0x00;
	bq27425_write_reg(client, OPCODE_SET1, &raw_data[1]);	
	pr_info("[BATT]read opcode func 2 \n");
	bq27425_read32_reg(client,TEMP_SET,data);

	ret = (data[6]) + (data[5]<<8);
					
	return ret;
		
}


unsigned int bq27425_get_temperature(void)
{
	struct i2c_client *client = fg_i2c_client;
	u8 data[2];
	int temper = 0;

	if (bq27425_read_reg(client, TEMP, data) < 0)
			return -1;
 
	   
	temper = ((data[0]) + (data[1]<<8))/10-273;//0.1K to C temperature

	return temper;
}
unsigned int bq27425_get_current(void)
{
	struct i2c_client *client = fg_i2c_client;
	u8 data[2];
	u32 bq_current ;

	if (bq27425_read_reg(client, AI, data) < 0)
			return -1;
	
	bq_current = ((data[0]) + (data[1]<<8));

	return bq_current;
}

// check 
static int bq27425_remove(struct i2c_client *client)
{
	struct bq27425_chip *chip = i2c_get_clientdata(client);

	//wake_lock_destroy(&chip->fuel_alert_wake_lock);	 /* not use only trebon*/

	omap_gpio_i2c_deinit_fuel(Bq27425_i2c_client);
	
	kfree(chip);
	
	return 0;
}

#define bq27425_suspend NULL
#define bq27425_resume NULL

static int fuel_gauge_init_client(struct i2c_client *client)
{
	/* Initialize the max17043 Chip */
//	init_waitqueue_head(&g_data_ready_wait_queue);
	return 0;
}


static int bq27425_probe(struct i2c_client *client, 
				const struct i2c_device_id *id)
{
	struct fuel_gauge_data *mt;
	int err = -1;
	int ret = 0;
	printk("[bq27425] ---probe!!!!!!!!!!!---\n");
	
	pr_info("[BATT] bq27425 probe: fuel_gauge_probe!!!!!!!!!!!!!!!!!!!!!!!!\n");


	//gpio_tlmm_config(GPIO_CFG(FUEL_I2C_SDA , 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP,GPIO_CFG_2MA), GPIO_CFG_ENABLE);
	//gpio_tlmm_config(GPIO_CFG(FUEL_I2C_SCL , 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP,GPIO_CFG_2MA), GPIO_CFG_ENABLE);
	
	Bq27425_i2c_client = omap_gpio_i2c_init_fuel(OMAP_GPIO_FUEL_SDA, OMAP_GPIO_FUEL_SCL, 0x55, 10);
	if(!Bq27425_i2c_client)
	{
		printk("[bq27425] omap_gpio_i2c_init_fuel fails.\n");
	}

	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		goto exit_check_functionality_failed;
	}

	if (!(mt = kzalloc(sizeof(struct fuel_gauge_data), GFP_KERNEL)))
	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	i2c_set_clientdata(client, mt);
	fuel_gauge_init_client(client);
	fg_i2c_client = client;

	//fuel_gauge_chip_init();
	
	is_attached = 1;

	ret = bq27425_get_opcode();
	printk("READ_OPCODE : %x \n", ret);
	printk("READ_SAMSUNG_OPCODE : %x \n", SAMSUNG_OPCODE); 
	

	if(ret == SAMSUNG_OPCODE)
		pr_info("%s [BATT] Data Write Sucess \n",__func__);
	else	
		pr_info("%s [BATT] Data Write Faild \n",__func__);

	pr_info("[BATT] %s : is ATTACHED %d!\n", __func__, is_attached);

	pr_info("[BATT] %s : success!\n", __func__);
	

	return 0;

exit_alloc_data_failed:
exit_check_functionality_failed:
	pr_err("%s: Error! (%d)\n", __func__, err);
	return err;
}

static const struct i2c_device_id bq27425_id[] = {
	{"bq27425", 0},
	{}
};
//MODULE_DEVICE_TABLE(i2c, fg_device_id);

static struct i2c_driver fg_i2c_driver = {
	.driver = {
		.name = "bq27425",
		.owner = THIS_MODULE,
	},
	.probe		= bq27425_probe,
	.remove		= bq27425_remove,
	.suspend	= bq27425_suspend,
	.resume		= bq27425_resume,
	.id_table	= bq27425_id,
};

 int  __init bq27425_init(void)
{
	int err;
	
	pr_info("[bq27425] ---------------- bq27425_init entry ---------------- \n");
	printk("[bq27425] ---init!!!!!!!!!!!---\n");
	
	err = i2c_add_driver(&fg_i2c_driver);
	if (err)
	{
		pr_info("[bq27425] Adding bq27425 driver failed "
		       "(errno = %d)\n", err);
		printk("[bq27425] Adding bq27425 driver failed \n ");
	}
	else
	{
		pr_info("[bq27425] Successfully added bq27425 driver %s, (err: %d)\n",
		       fg_i2c_driver.driver.name, err);
		printk("[bq27425] Successfully added bq27425 driver \n");
	}
	return err;	

}
//subsys_initcall(bq27425_init);
// module_init(bq27425_init);


 void __exit bq27425_exit(void)
{
	i2c_del_driver(&fg_i2c_driver);
}
//module_exit(bq27425_exit);

MODULE_DESCRIPTION("BQ27425 Fuel Gauge");
MODULE_LICENSE("GPL");


