/*
 * ili9486 LDI support
 *
 * Copyright (C) 2009 Samsung Corporation
 * Author: Samsung Electronics..
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/backlight.h>
#include <linux/i2c/twl.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <plat/gpio.h>
#include <plat/hardware.h>
#include <plat/mux.h>
#include <plat/control.h>
#include <plat/display.h>
#include <asm/mach-types.h>
#include <../../../../arch/arm/mach-omap2/mux.h>

//#define	LCD_DBG_ENABLE	1
#undef	LCD_DBG_ENABLE

#ifdef	LCD_DBG_ENABLE
#define	DEBUG_LCD(fmt,args...)	printk(fmt,##args)
#else
#define	DEBUG_LCD(fmt,args...)	do{}	while(0)
#endif


//#define ENABLE_ESD_WDOG

#ifdef ENABLE_ESD_WDOG
void init_lcd_esd_wdog(void);
static void deinit_esd_wdog(void);
static void enable_esd_wdog(void);
static void disable_esd_wdog(void);

struct work_struct lcd_esd_work;

static int g_lcd_exist = false;
void ili9486_ldi_poweroff_smd(void);
void ili9486_ldi_poweron_smd(void);

static int off_by_normal_proc = 0;
#endif

#define LCD_XRES		480
#define LCD_YRES		800

static int current_panel = 0;	// 0:smd
static int lcd_enabled = 0;
static int is_nt35510_spi_shutdown = 0;

static int current_intensity = 115;	// DEFAULT BRIGHTNESS

// default setting : smd panel. 
static u16 LCD_HBP =	9;
static u16 LCD_HFP =	4; 
static u16 LCD_HSW =	2; 
static u16 LCD_VBP =	0xA;
static u16 LCD_VFP =	0xE;
static u16 LCD_VSW =	2; 

#define LCD_PIXCLOCK_MAX	        25000 // 15.4mhz


#define GPIO_LEVEL_LOW   0
#define GPIO_LEVEL_HIGH  1

#define POWER_OFF	0	// set in lcd_poweroff function.
#define POWER_ON	1	// set in lcd_poweron function

static struct spi_device *nt35510lcd_spi;
    

static atomic_t lcd_power_state = ATOMIC_INIT(POWER_ON);	// default is power on because bootloader already turn on LCD.
static atomic_t ldi_power_state = ATOMIC_INIT(POWER_ON);	// default is power on because bootloader already turn on LCD.

int g_lcdlevel = 0x6C;

// ------------------------------------------ // 
//          For Regulator Framework                            //
// ------------------------------------------ // 

struct regulator *vaux2;
struct regulator *vaux4;

#define MAX_NOTIFICATION_HANDLER	10

void nt35510_lcd_poweron(void);
void nt35510_lcd_poweroff(void);
void nt35510_lcd_LDO_on(void);
void nt35510_lcd_LDO_off(void);


// paramter : POWER_ON or POWER_OFF
typedef void (*notification_handler)(const int);
typedef struct
{
	int  state;
	spinlock_t vib_lock;
}timer_state_t;
timer_state_t timer_state;


notification_handler power_state_change_handler[MAX_NOTIFICATION_HANDLER];

int nt35510_add_power_state_monitor(notification_handler handler);
void nt35510_remove_power_state_monitor(notification_handler handler);

EXPORT_SYMBOL(nt35510_add_power_state_monitor);
EXPORT_SYMBOL(nt35510_remove_power_state_monitor);

static void nt35510_notify_power_state_changed(void);
static void aat1402_set_brightness(void);
extern int omap34xx_pad_set_config_lcd(u16,u16);

#define PM_RECEIVER                     TWL4030_MODULE_PM_RECEIVER

#define ENABLE_VPLL2_DEDICATED          0x05
#define ENABLE_VPLL2_DEV_GRP            0x20
#define TWL4030_VPLL2_DEV_GRP           0x33
#define TWL4030_VPLL2_DEDICATED       	0x36


/*  Manual
 * defines HFB, HSW, HBP, VFP, VSW, VBP as shown below
 */
static struct omap_video_timings panel_timings = {0,};


void lcd_en_set(int set)
{
	if (set == 0)
	{
		gpio_set_value(OMAP_GPIO_NEW_LCD_EN_SET, GPIO_LEVEL_LOW);
	}
	else
	{
		gpio_set_value(OMAP_GPIO_NEW_LCD_EN_SET, GPIO_LEVEL_HIGH);
	}
}

int nt35510_add_power_state_monitor(notification_handler handler)
{
	int index = 0;
	if(handler == NULL)
	{
		DEBUG_LCD(KERN_ERR "[LCD][%s] param is null\n", __func__);
		return -EINVAL;
	}

	for(; index < MAX_NOTIFICATION_HANDLER; index++)
	{
		if(power_state_change_handler[index] == NULL)
		{
			power_state_change_handler[index] = handler;
			return 0;
		}
	}

	// there is no space this time
	DEBUG_LCD(KERN_INFO "[LCD][%s] No spcae\n", __func__);

	return -ENOMEM;
}

void nt35510_remove_power_state_monitor(notification_handler handler)
{
	int index = 0;
	if(handler == NULL)
	{
		DEBUG_LCD(KERN_ERR "[LCD][%s] param is null\n", __func__);
		return;
	}
	
	for(; index < MAX_NOTIFICATION_HANDLER; index++)
	{
		if(power_state_change_handler[index] == handler)
		{
			power_state_change_handler[index] = NULL;
		}
	}
}
	
static void nt35510_notify_power_state_changed(void)
{
	int index = 0;
	for(; index < MAX_NOTIFICATION_HANDLER; index++)
	{
		if(power_state_change_handler[index] != NULL)
		{
			power_state_change_handler[index](atomic_read(&lcd_power_state));
		}
	}

}

static __init int setup_current_panel(char *opt)
{
	current_panel = (u32)memparse(opt, &opt);
	return 0;
}
__setup("androidboot.current_panel=", setup_current_panel);

static int nt35510_panel_probe(struct omap_dss_device *dssdev)
{
	DEBUG_LCD(KERN_INFO " **** nt35510_panel_probe.\n");
		
	vaux4 = regulator_get( &dssdev->dev, "vaux4" );
	if( IS_ERR( vaux4 ) )
		DEBUG_LCD( "Fail to register vaux4 using regulator framework!\n" );	

	vaux2 = regulator_get( &dssdev->dev, "vaux2" );
	if( IS_ERR( vaux2 ) )
		DEBUG_LCD( "Fail to register vaux2 using regulator framework!\n" );	

	nt35510_lcd_LDO_on();

	//MLCD pin set to OUTPUT.
	if (gpio_request(OMAP_GPIO_MLCD_RST, "MLCD_RST") < 0) {
		DEBUG_LCD(KERN_ERR "\n FAILED TO REQUEST GPIO %d \n", OMAP_GPIO_MLCD_RST);
		return;
	}
	gpio_direction_output(OMAP_GPIO_MLCD_RST, 1);

	/* 320 x 480 @ 90 Hz */
	panel_timings.x_res          = LCD_XRES,
	panel_timings.y_res          = LCD_YRES,
	panel_timings.pixel_clock    = LCD_PIXCLOCK_MAX,
	panel_timings.hfp            = LCD_HFP,
	panel_timings.hsw            = LCD_HSW,
	panel_timings.hbp            = LCD_HBP,
	panel_timings.vfp            = LCD_VFP,
	panel_timings.vsw            = LCD_VSW,
	panel_timings.vbp            = LCD_VBP;

	//dssdev->panel.config = OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_ONOFF | OMAP_DSS_LCD_IPC;
	dssdev->panel.config = OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS | OMAP_DSS_LCD_IPC |
					OMAP_DSS_LCD_IHS | OMAP_DSS_LCD_ONOFF | OMAP_DSS_LCD_IEO;

	//dssdev->panel.recommended_bpp= 32;  /* 35 kernel  recommended_bpp field is removed */
	dssdev->panel.acb = 0;
	dssdev->panel.timings = panel_timings;
	dssdev->ctrl.pixel_size = 24;
	
	return 0;
}

static void nt35510_panel_remove(struct omap_dss_device *dssdev)
{
	regulator_put( vaux4 );
	regulator_put( vaux2 );
}

static int nt35510_panel_enable(struct omap_dss_device *dssdev)
{
	int r = 0;

	/* HW test team ask power on reset should be done before enabling
	   sync signals. So it's not done with lcd poweron code */
	if (lcd_enabled == 0)	/* don't reset lcd for initial booting */
	{
		nt35510_lcd_LDO_on();

		// Activate Reset
		gpio_set_value(OMAP_GPIO_MLCD_RST, GPIO_LEVEL_LOW);	
		mdelay(1);
		gpio_set_value(OMAP_GPIO_MLCD_RST, GPIO_LEVEL_HIGH);
		mdelay(5);
		
		//MLCD pin set to InputPulldown.
		omap_ctrl_writew(0x010C, 0x1c6);
	}

	r = omapdss_dpi_display_enable(dssdev);
	if (r)
		goto err0;

	/* Delay recommended by panel DATASHEET */
	mdelay(4);
	if (dssdev->platform_enable) {
		r = dssdev->platform_enable(dssdev);
		if (r)
			goto err1;
        }
        dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;
		
	if(lcd_enabled ==1)
		lcd_enabled =0;
	else 
		nt35510_lcd_poweron();

	return r;
err1:
	omapdss_dpi_display_disable(dssdev);
err0:
	return r;
}

static void nt35510_panel_disable(struct omap_dss_device *dssdev)
{
	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;
	if (is_nt35510_spi_shutdown == 1)
	{
		DEBUG_LCD("[%s] skip omapdss_dpi_display_disable..\n", __func__);
		return;
	}
	nt35510_lcd_poweroff();
	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);
	mdelay(4);

	omapdss_dpi_display_disable(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static int nt35510_panel_suspend(struct omap_dss_device *dssdev)
{
	DEBUG_LCD(KERN_INFO " **** nt35510_panel_suspend\n");

#ifdef ENABLE_ESD_WDOG
	disable_esd_wdog();
#endif

	spi_setup(nt35510lcd_spi);

	//gpio_set_value(OMAP_GPIO_LCD_EN_SET, GPIO_LEVEL_LOW);
	lcd_en_set(0);
	mdelay(1);

    #if 0
	nt35510_lcd_poweroff();
    #else
//	nt35510_ldi_standby();
	nt35510_panel_disable(dssdev);
    #endif
   
	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;
    return 0;
}

static int nt35510_panel_resume(struct omap_dss_device *dssdev)
{
	DEBUG_LCD(KERN_INFO " **** nt35510_panel_resume\n");
	//TSP power control
	//ts_power_control(NULL, 1);
	//zinitix_resume();
	spi_setup(nt35510lcd_spi);
    
//	msleep(150);
    
    #if 0
	nt35510_lcd_poweron();
    #else
//	nt35510_ldi_wakeup();
	nt35510_panel_enable(dssdev);
    #endif

	//gpio_set_value(OMAP_GPIO_LCD_EN_SET, GPIO_LEVEL_LOW);
	lcd_en_set(0);
	mdelay(1);
	//gpio_set_value(OMAP_GPIO_LCD_EN_SET, GPIO_LEVEL_HIGH);
	lcd_en_set(1);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

#ifdef ENABLE_ESD_WDOG
	enable_esd_wdog();
#endif

	return 0;
}

static int nt35510_get_recommended_bpp(struct omap_dss_device *dssdev)
{
	return 24;	/* AALTO lcd has 18bit data line but internally processed by packed 24bit */
}

static struct omap_dss_driver nt35510_driver = {
	.probe          = nt35510_panel_probe,
	.remove         = nt35510_panel_remove,

	.enable         = nt35510_panel_enable,
	.disable        = nt35510_panel_disable,
	.suspend        = nt35510_panel_suspend,
	.resume         = nt35510_panel_resume,

	.get_recommended_bpp = nt35510_get_recommended_bpp,

	.driver		= {
		.name	= "nt35510_panel",
		.owner 	= THIS_MODULE,
	},
};

static void spi1writeindex(u8 index)
{
	volatile unsigned short cmd = 0;
	cmd= 0x0000|index;

	spi_write(nt35510lcd_spi,(unsigned char*)&cmd,2);

	udelay(100);
	udelay(100);
}

static void spi1writedata(u8 data)
{
	volatile unsigned short datas = 0;
	datas= 0x0100|data;
	spi_write(nt35510lcd_spi,(unsigned char*)&datas,2);

	udelay(100);
	udelay(100);
}

static void spi1write(u8 index, u8 data)
{
	volatile unsigned short cmd = 0;
	volatile unsigned short datas=0;

	cmd = 0x0000 | index;
	datas = 0x0100 | data;
	
	spi_write(nt35510lcd_spi,(unsigned char*)&cmd,2);
	udelay(100);
	spi_write(nt35510lcd_spi,(unsigned char *)&datas,2);
	udelay(100);
	udelay(100);
}

static int is_lcd_exist(void)
{
	u16 cmd = 0x04;

	u32 buf = 0;

	spi_write_then_read(nt35510lcd_spi, (u8*)&cmd, 2, (u8*)&buf, 3); 

	DEBUG_LCD("%s : 0x%x\n", __func__, buf);

	return true;
}

void ili9486_ldi_poweron_smd(void)
{
	DEBUG_LCD("[LCD] %s()\n", __func__);

	mdelay(10);

	/* User set */
	spi1writeindex(0xFF); spi1writedata(0xAA); spi1writedata(0x55); spi1writedata(0x25); spi1writedata(0x01);

	spi1writeindex(0xF3); 
	spi1writedata(0x00); spi1writedata(0x32); spi1writedata(0x00); spi1writedata(0x38); spi1writedata(0x31); 
	spi1writedata(0x08); spi1writedata(0x11); spi1writedata(0x00);

	spi1writeindex(0xF0); 
 	spi1writedata(0x55); spi1writedata(0xAA); spi1writedata(0x52); spi1writedata(0x08); spi1writedata(0x00);

	spi1writeindex(0xB0);
	spi1writedata(0x04); spi1writedata(0x0A); spi1writedata(0x0E); spi1writedata(0x09); spi1writedata(0x04);

	spi1writeindex(0xB1); spi1writedata(0xCC); spi1writedata(0x04);
	spi1writeindex(0x36); spi1writedata(0x02);
	spi1writeindex(0xB3); spi1writedata(0x00);
	spi1writeindex(0xB6); spi1writedata(0x03);
	spi1writeindex(0xB7); spi1writedata(0x70); spi1writedata(0x70); 
	spi1writeindex(0xB8); spi1writedata(0x00); spi1writedata(0x06); spi1writedata(0x06); spi1writedata(0x06);
	spi1writeindex(0xBC); spi1writedata(0x00); spi1writedata(0x00); spi1writedata(0x00);

	spi1writeindex(0xBD);
	spi1writedata(0x01); spi1writedata(0x84); spi1writedata(0x06); spi1writedata(0x50); spi1writedata(0x00);

	spi1writeindex(0xCC); spi1writedata(0X03); spi1writedata(0x2A); spi1writedata(0x06);

	/* Power Set*/
	spi1writeindex(0xF0);
	spi1writedata(0x55); spi1writedata(0xAA); spi1writedata(0x52); spi1writedata(0x08); spi1writedata(0x01);

	spi1writeindex(0xB0); spi1writedata(0x05); spi1writedata(0x05); spi1writedata(0x05);
	spi1writeindex(0xB1); spi1writedata(0x05); spi1writedata(0x05); spi1writedata(0x05);
	spi1writeindex(0xB2); spi1writedata(0x03); spi1writedata(0x03); spi1writedata(0x03);

	spi1writeindex(0xB8); spi1writedata(0x24); spi1writedata(0x24); spi1writedata(0x24);
	spi1writeindex(0xB3); spi1writedata(0x0A); spi1writedata(0x0A); spi1writedata(0X0A);
	spi1writeindex(0xB9); spi1writedata(0x24); spi1writedata(0x24); spi1writedata(0x24);
	spi1writeindex(0xBF); spi1writedata(0x01);
	spi1writeindex(0xB5); spi1writedata(0x08); spi1writedata(0x08); spi1writedata(0x08);
	spi1writeindex(0xB4); spi1writedata(0x2D); spi1writedata(0x2D); spi1writedata(0x2D);
	spi1writeindex(0xBC); spi1writedata(0x00); spi1writedata(0x68); spi1writedata(0x00);
	spi1writeindex(0xBD); spi1writedata(0x00); spi1writedata(0x7C); spi1writedata(0x00);
	spi1writeindex(0xBE); spi1writedata(0x00); spi1writedata(0x60);

	spi1writeindex(0xCE);
	spi1writedata(0x00); spi1writedata(0x00); spi1writedata(0x00); spi1writedata(0x00); spi1writedata(0x00); 
	spi1writedata(0x00); spi1writedata(0x00);

	/*Gamma Contral*/
	spi1writeindex(0xD0);
	spi1writedata(0x09); spi1writedata(0x0F); spi1writedata(0x06); spi1writedata(0x08);

	spi1writeindex(0xD1);
	spi1writedata(0x00); spi1writedata(0x37); spi1writedata(0x00); spi1writedata(0x61); spi1writedata(0x00);
	spi1writedata(0x92); spi1writedata(0x00); spi1writedata(0xB4); spi1writedata(0x00); spi1writedata(0xBF);
	spi1writedata(0x01); spi1writedata(0x06); spi1writedata(0x01); spi1writedata(0x30); spi1writedata(0x01);
	spi1writedata(0x75); spi1writedata(0x01); spi1writedata(0x94); spi1writedata(0x01); spi1writedata(0xC8);
	spi1writedata(0x01); spi1writedata(0xF5); spi1writedata(0x02); spi1writedata(0x30); spi1writedata(0x02);
	spi1writedata(0x6C); spi1writedata(0x02); spi1writedata(0x6E); spi1writedata(0x02); spi1writedata(0xA5);
	spi1writedata(0x02); spi1writedata(0xD1); spi1writedata(0x02); spi1writedata(0xFA); spi1writedata(0x03);
	spi1writedata(0x30); spi1writedata(0x03); spi1writedata(0x40); spi1writedata(0x03); spi1writedata(0x5E);
	spi1writedata(0x03); spi1writedata(0x77); spi1writedata(0x03); spi1writedata(0x94); spi1writedata(0x03);
	spi1writedata(0x9F); spi1writedata(0x03); spi1writedata(0xAC); spi1writedata(0x03); spi1writedata(0xBA);
	spi1writedata(0x03); spi1writedata(0xF1);

	spi1writeindex(0xD2);
	spi1writedata(0x00); spi1writedata(0x37); spi1writedata(0x00); spi1writedata(0x61); spi1writedata(0x00);
	spi1writedata(0x92); spi1writedata(0x00); spi1writedata(0xB4); spi1writedata(0x00); spi1writedata(0xBF);
	spi1writedata(0x01); spi1writedata(0x06); spi1writedata(0x01); spi1writedata(0x30); spi1writedata(0x01);
	spi1writedata(0x75); spi1writedata(0x01); spi1writedata(0x94); spi1writedata(0x01); spi1writedata(0xC8);
	spi1writedata(0x01); spi1writedata(0xF5); spi1writedata(0x02); spi1writedata(0x30); spi1writedata(0x02);
	spi1writedata(0x6C); spi1writedata(0x02); spi1writedata(0x6E); spi1writedata(0x02); spi1writedata(0xA5);
	spi1writedata(0x02); spi1writedata(0xD1); spi1writedata(0x02); spi1writedata(0xFA); spi1writedata(0x03);
	spi1writedata(0x30); spi1writedata(0x03); spi1writedata(0x40); spi1writedata(0x03); spi1writedata(0x5E);
	spi1writedata(0x03); spi1writedata(0x77); spi1writedata(0x03); spi1writedata(0x94); spi1writedata(0x03);
	spi1writedata(0x9F); spi1writedata(0x03); spi1writedata(0xAC); spi1writedata(0x03); spi1writedata(0xBA);
	spi1writedata(0x03); spi1writedata(0xF1);

	spi1writeindex(0xD3);
	spi1writedata(0x00); spi1writedata(0x37); spi1writedata(0x00); spi1writedata(0x61); spi1writedata(0x00);
	spi1writedata(0x92); spi1writedata(0x00); spi1writedata(0xB4); spi1writedata(0x00); spi1writedata(0xBF);
	spi1writedata(0x01); spi1writedata(0x06); spi1writedata(0x01); spi1writedata(0x30); spi1writedata(0x01);
	spi1writedata(0x75); spi1writedata(0x01); spi1writedata(0x94); spi1writedata(0x01); spi1writedata(0xC8);
	spi1writedata(0x01); spi1writedata(0xF5); spi1writedata(0x02); spi1writedata(0x30); spi1writedata(0x02);
	spi1writedata(0x6C); spi1writedata(0x02); spi1writedata(0x6E); spi1writedata(0x02); spi1writedata(0xA5);
	spi1writedata(0x02); spi1writedata(0xD1); spi1writedata(0x02); spi1writedata(0xFA); spi1writedata(0x03);
	spi1writedata(0x30); spi1writedata(0x03); spi1writedata(0x40); spi1writedata(0x03); spi1writedata(0x5E);
	spi1writedata(0x03); spi1writedata(0x77); spi1writedata(0x03); spi1writedata(0x94); spi1writedata(0x03);
	spi1writedata(0x9F); spi1writedata(0x03); spi1writedata(0xAC); spi1writedata(0x03); spi1writedata(0xBA);
	spi1writedata(0x03); spi1writedata(0xF1);

	spi1writeindex(0xD4);
	spi1writedata(0x00); spi1writedata(0x37); spi1writedata(0x00); spi1writedata(0x68); spi1writedata(0x00);
	spi1writedata(0xA0); spi1writedata(0x00); spi1writedata(0xEF); spi1writedata(0x01); spi1writedata(0x1A);
	spi1writedata(0x01); spi1writedata(0x3E); spi1writedata(0x01); spi1writedata(0x4E); spi1writedata(0x01);
	spi1writedata(0x60); spi1writedata(0x01); spi1writedata(0x92); spi1writedata(0x01); spi1writedata(0xD0);
	spi1writedata(0x01); spi1writedata(0xF7); spi1writedata(0x02); spi1writedata(0x45); spi1writedata(0x02);
	spi1writedata(0x70); spi1writedata(0x02); spi1writedata(0x72); spi1writedata(0x02); spi1writedata(0xA8);
	spi1writedata(0x02); spi1writedata(0xE0); spi1writedata(0x02); spi1writedata(0xF7); spi1writedata(0x03);
	spi1writedata(0x2A); spi1writedata(0x03); spi1writedata(0x2D); spi1writedata(0x03); spi1writedata(0x5E);
	spi1writedata(0x03); spi1writedata(0x77); spi1writedata(0x03); spi1writedata(0x94); spi1writedata(0x03);
	spi1writedata(0x9F); spi1writedata(0x03); spi1writedata(0xAC); spi1writedata(0x03); spi1writedata(0xBA);
	spi1writedata(0x03); spi1writedata(0xF1);

	spi1writeindex(0xD5);
	spi1writedata(0x00); spi1writedata(0x37); spi1writedata(0x00); spi1writedata(0x68); spi1writedata(0x00);
	spi1writedata(0xA0); spi1writedata(0x00); spi1writedata(0xEF); spi1writedata(0x01); spi1writedata(0x1A);
	spi1writedata(0x01); spi1writedata(0x3E); spi1writedata(0x01); spi1writedata(0x4E); spi1writedata(0x01);
	spi1writedata(0x60); spi1writedata(0x01); spi1writedata(0x92); spi1writedata(0x01); spi1writedata(0xD0);
	spi1writedata(0x01); spi1writedata(0xF7); spi1writedata(0x02); spi1writedata(0x45); spi1writedata(0x02);
	spi1writedata(0x70); spi1writedata(0x02); spi1writedata(0x72); spi1writedata(0x02); spi1writedata(0xA8);
	spi1writedata(0x02); spi1writedata(0xE0); spi1writedata(0x02); spi1writedata(0xF7); spi1writedata(0x03);
	spi1writedata(0x2A); spi1writedata(0x03); spi1writedata(0x2D); spi1writedata(0x03); spi1writedata(0x5E);
	spi1writedata(0x03); spi1writedata(0x77); spi1writedata(0x03); spi1writedata(0x94); spi1writedata(0x03);
	spi1writedata(0x9F); spi1writedata(0x03); spi1writedata(0xAC); spi1writedata(0x03); spi1writedata(0xBA);
	spi1writedata(0x03); spi1writedata(0xF1);

	spi1writeindex(0xD6);
	spi1writedata(0x00); spi1writedata(0x37); spi1writedata(0x00); spi1writedata(0x68); spi1writedata(0x00);
	spi1writedata(0xA0); spi1writedata(0x00); spi1writedata(0xEF); spi1writedata(0x01); spi1writedata(0x1A);
	spi1writedata(0x01); spi1writedata(0x3E); spi1writedata(0x01); spi1writedata(0x4E); spi1writedata(0x01);
	spi1writedata(0x60); spi1writedata(0x01); spi1writedata(0x92); spi1writedata(0x01); spi1writedata(0xD0);
	spi1writedata(0x01); spi1writedata(0xF7); spi1writedata(0x02); spi1writedata(0x45); spi1writedata(0x02);
	spi1writedata(0x70); spi1writedata(0x02); spi1writedata(0x72); spi1writedata(0x02); spi1writedata(0xA8);
	spi1writedata(0x02); spi1writedata(0xE0); spi1writedata(0x02); spi1writedata(0xF7); spi1writedata(0x03);
	spi1writedata(0x2A); spi1writedata(0x03); spi1writedata(0x2D); spi1writedata(0x03); spi1writedata(0x5E);
	spi1writedata(0x03); spi1writedata(0x77); spi1writedata(0x03); spi1writedata(0x94); spi1writedata(0x03);
	spi1writedata(0x9F); spi1writedata(0x03); spi1writedata(0xAC); spi1writedata(0x03); spi1writedata(0xBA);
	spi1writedata(0x03); spi1writedata(0xF1);

	/*Manufacture command set selection*/
	spi1writeindex(0xF0);
	spi1writedata(0x55); spi1writedata(0xAA); spi1writedata(0x52); spi1writedata(0x08); spi1writedata(0x00);

	spi1writeindex(0xE0);
	spi1writedata(0x01); spi1writedata(0x01);	/* pwm as 39.06 khz */

	/*brightness*/
	spi1writeindex(0x51);
	spi1writedata(0x43);
	
	spi1writeindex(0x53);
	spi1writedata(0x2c);
	/*Display On*/
	spi1writeindex(0x11);
	
	msleep(120);

	spi1writeindex(0x29);

	/* LCD BL_CTRL high for aat1402 */
	//gpio_set_value(OMAP_GPIO_LCD_EN_SET, GPIO_LEVEL_HIGH);
	lcd_en_set(1);
	mdelay(1);

	atomic_set(&ldi_power_state, POWER_ON);
}

void ili9486_ldi_poweroff_smd(void)
{
	DEBUG_LCD("[LCD] %s\n", __func__);

	/* LCD BL_CTRL low for aat1042 */
	//gpio_set_value(OMAP_GPIO_LCD_EN_SET, GPIO_LEVEL_LOW);
	lcd_en_set(0);
	mdelay(1);

	/* display off */
	spi1writeindex(0x28); 
	msleep(1);

	/* sleep in */
	spi1writeindex(0x10); 
	msleep(50);
	/* booster stop and internal oscillator stop here...by LCD hw */
	msleep(120);
	
	atomic_set(&ldi_power_state, POWER_OFF);
}

/* called by fbsysfs */
void samsung_aalto_set_cabc(const char* level)
{
	int cabc_level = simple_strtol(level, NULL, 10);

	if (cabc_level == 0)
	{
		DEBUG_LCD("%s : cabc off\n", __func__);
		spi1writeindex(0x55);	
		spi1writedata(0x00);	/* cabc off */
	}
	else
	{
		spi1writeindex(0x55);	
		spi1writedata(0x03);	/* cabc on for moving image */

		if (cabc_level >= 70 && cabc_level <= 100)
		{
			cabc_level = (-cabc_level + 100) >> 1;	/* see 248pp of ILI9486 manual */
			cabc_level <<= 4;	
		}
		else
		{
			cabc_level = 0x70;	/* default value (86%) for abnormal input */
		}
		
		spi1writeindex(0xc9);	
		spi1writedata(cabc_level);	/* THRES_MOV = cabc_level */

		DEBUG_LCD("%s : cabc = 0x%x\n", __func__, cabc_level);
	}
}

void nt35510_lcd_LDO_on(void)
{
	int ret;

	DEBUG_LCD("+++ %s\n", __func__);
//	twl_i2c_read_regdump();
#if 1	
	ret = regulator_enable( vaux2 ); //VAUX2 - 1.8V
	if ( ret )
		DEBUG_LCD("Regulator vaux2 error!!\n");
#else
	twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0x00, 0x1F);
	twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0x1, 0x22);
	twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0xe0, 0x1F);
#endif

	mdelay(1);

	ret = regulator_enable( vaux4 ); //VAUX4 - 3.0V
	if ( ret )
		DEBUG_LCD("Regulator vaux4 error!!\n");

	mdelay(1);
	
	DEBUG_LCD("--- %s\n", __func__);
}

void nt35510_lcd_LDO_off(void)
{
	int ret;

	DEBUG_LCD("+++ %s\n", __func__);

	// Reset Release (reset = L)
	gpio_set_value(OMAP_GPIO_MLCD_RST, GPIO_LEVEL_LOW); 
	mdelay(10);

	// VCI 3.0V OFF
	ret = regulator_disable( vaux4 );
	if ( ret )
		DEBUG_LCD("Regulator vaux4 error!!\n");
	mdelay(1);

	// VDD3 1.8V OFF
#if 1
	ret = regulator_disable( vaux2 );
	if ( ret )
		DEBUG_LCD("Regulator vaux2 error!!\n");
#else
	twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0x00, 0x1F);
#endif	

	DEBUG_LCD("--- %s\n", __func__);
}

void nt35510_lcd_poweroff(void)
{
	ili9486_ldi_poweroff_smd();

	// turn OFF VCI (3.0V)
	// turn OFF VDD3 (1.8V)
	nt35510_lcd_LDO_off();

#if 1             
		omap_mux_init_signal("mcspi1_clk", OMAP_MUX_MODE7 | OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mcspi1_simo",  OMAP_MUX_MODE7 | OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mcspi1_somi", OMAP_MUX_MODE7 | OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mcspi1_cs0", OMAP_MUX_MODE7 | OMAP_PIN_INPUT_PULLDOWN);
#endif		
}

void nt35510_lcd_poweron(void)
{
	u8 read=0;

#if 1	
	omap_mux_init_signal("mcspi1_clk",OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_signal("mcspi1_simo",OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLDOWN);
	omap_mux_init_signal("mcspi1_somi", OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLDOWN);
	omap_mux_init_signal("mcspi1_cs0", OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP);
#endif	

	ili9486_ldi_poweron_smd();

	aat1402_set_brightness();
}

// [[ backlight control 
//static int current_intensity = 115;	// DEFAULT BRIGHTNESS
static DEFINE_SPINLOCK(aat1402_bl_lock);

static void aat1402_set_brightness(void)
{
	//DEBUG_LCD(KERN_DEBUG" *** aat1402_set_brightness : %d\n", current_intensity);
	DEBUG_LCD(" *** aat1402_set_brightness : %d\n", current_intensity);

	//spin_lock_irqsave(&aat1402_bl_lock, flags);
	//spin_lock(&aat1402_bl_lock);

	spi1writeindex(0x51);
	spi1writedata(current_intensity);	// PWM control. default brightness : 115

	//spin_unlock_irqrestore(&aat1402_bl_lock, flags);
	//spin_unlock(&aat1402_bl_lock);

}

static int aat1402_bl_get_intensity(struct backlight_device *bd)
{
	return current_intensity;
}

static int aat1402_bl_set_intensity(struct backlight_device *bd)
{
	int intensity = bd->props.brightness;
	
	if( intensity < 0 || intensity > 255 )
		return;

/*
	while(atomic_read(&ldi_power_state)==POWER_OFF) 
	{
		if(--retry_count == 0)
			break;
		mdelay(5);
	}
*/	
	current_intensity = intensity;

	if(atomic_read(&ldi_power_state)==POWER_OFF) 
	{
		return;
	}

#ifdef ENABLE_ESD_WDOG
	/* This is to avoid lcd_chk_work_handler() reset lcd 
	   when LCD is turned off by normal procedure (ex. sleep) */
	if (intensity == 0)
	{
		off_by_normal_proc = 1;
	}
	else 
	{
		off_by_normal_proc = 0;
	}
#endif

	aat1402_set_brightness();

	return 0;
}

static struct backlight_ops aat1402_bl_ops = {
	.get_brightness = aat1402_bl_get_intensity,
	.update_status  = aat1402_bl_set_intensity,
};


#ifdef ENABLE_ESD_WDOG

void lcd_esd_work_handler(struct work_struct *work)
{
	if(atomic_read(&ldi_power_state) != POWER_OFF) 
	{
		// Activate Reset
		gpio_set_value(OMAP_GPIO_MLCD_RST, GPIO_LEVEL_LOW);	
		mdelay(1);
		gpio_set_value(OMAP_GPIO_MLCD_RST, GPIO_LEVEL_HIGH);
		mdelay(5);
		
		//MLCD pin set to InputPulldown.
		omap_ctrl_writew(0x010C, 0x1c6);

		ili9486_ldi_poweron_smd();
		aat1402_set_brightness();

		DEBUG_LCD("%s: reset lcd!! - \n", __func__);
	}
	else
	{
		DEBUG_LCD("%s : lcd is in off state.. so no need to reset. \n", __func__);
	}

	enable_irq(gpio_to_irq(OMAP_GPIO_LCD_ESD_DET));
}

static irqreturn_t esd_wdog_handler(int irq, void *handle)
{
	disable_irq_nosync(gpio_to_irq(OMAP_GPIO_LCD_ESD_DET));

	schedule_work(&lcd_esd_work);

	return IRQ_HANDLED;
}

void init_lcd_esd_wdog(void)
{
	DEBUG_LCD("%s\n", __func__);

	if (0 == request_irq(gpio_to_irq(OMAP_GPIO_LCD_ESD_DET), esd_wdog_handler, 
						IRQF_TRIGGER_LOW, "LCD_ESD_DET", NULL))
	{
		g_lcd_exist = true;
	}
	else
	{
		DEBUG_LCD("%s : failed!\n", __func__);
		g_lcd_exist = false;
	}
}
EXPORT_SYMBOL(init_lcd_esd_wdog);

static void deinit_esd_wdog(void)
{
	DEBUG_LCD("%s\n", __func__);

	if (g_lcd_exist == true)
	{
		free_irq(gpio_to_irq(OMAP_GPIO_LCD_ESD_DET), NULL);
	}
}

static void disable_esd_wdog(void)
{
	DEBUG_LCD("%s\n", __func__);

	if (g_lcd_exist == true)
	{
		disable_irq(gpio_to_irq(OMAP_GPIO_LCD_ESD_DET));
	}
}

static void enable_esd_wdog(void)
{
	DEBUG_LCD("%s\n", __func__);

	if (g_lcd_exist == true)
	{
		enable_irq(gpio_to_irq(OMAP_GPIO_LCD_ESD_DET));
	}
}

#endif	/* ENABLE_ESD_WDOG */

static int nt35510_spi_probe(struct spi_device *spi)
{
    struct backlight_properties props;
     int status =0;
		
	DEBUG_LCD(KERN_INFO " **** nt35510_spi_probe.\n");
	nt35510lcd_spi = spi;
	nt35510lcd_spi->mode = SPI_MODE_0;
	nt35510lcd_spi->bits_per_word = 9 ;

	DEBUG_LCD(" nt35510lcd_spi->chip_select = %x\t, mode = %x\n", nt35510lcd_spi->chip_select,  nt35510lcd_spi->mode);
	DEBUG_LCD(" ax_speed_hz  = %x\t modalias = %s", nt35510lcd_spi->max_speed_hz, nt35510lcd_spi->modalias );
	
	status = spi_setup(nt35510lcd_spi);
	DEBUG_LCD(" spi_setup ret = %x\n",status );
	
	omap_dss_register_driver(&nt35510_driver);
//	led_classdev_register(&spi->dev, &nt35510_backlight_led);
	struct backlight_device *bd;
	bd = backlight_device_register("omap_bl", &spi->dev, NULL, &aat1402_bl_ops, &props);
	bd->props.max_brightness = 255;
	bd->props.brightness = 125;
	
#ifndef CONFIG_FB_OMAP_BOOTLOADER_INIT
	lcd_enabled = 0;
	nt35510_lcd_poweron();
#else
	lcd_enabled =1;
#endif

#ifdef ENABLE_ESD_WDOG
	INIT_WORK(&lcd_esd_work, lcd_esd_work_handler);
#endif

	return 0;
}

static int nt35510_spi_remove(struct spi_device *spi)
{
//	led_classdev_unregister(&nt35510_backlight_led);
	omap_dss_unregister_driver(&nt35510_driver);

#ifdef ENABLE_ESD_WDOG
	deinit_esd_wdog();
#endif

	return 0;
}
static void nt35510_spi_shutdown(struct spi_device *spi)
{
	DEBUG_LCD("*** First power off LCD.\n");
	is_nt35510_spi_shutdown = 1;
	nt35510_lcd_poweroff();
	DEBUG_LCD("*** power off - backlight.\n");
	//gpio_set_value(OMAP_GPIO_LCD_EN_SET, GPIO_LEVEL_LOW);
	lcd_en_set(0);
}

static int nt35510_spi_suspend(struct spi_device *spi, pm_message_t mesg)
{
    //spi_send(spi, 2, 0x01);  /* R2 = 01h */
    //mdelay(40);

#if 0
	nt35510lcd_spi = spi;
	nt35510lcd_spi->mode = SPI_MODE_0;
	nt35510lcd_spi->bits_per_word = 16 ;
	spi_setup(nt35510lcd_spi);

	lcd_poweroff();
	zeus_panel_power_enable(0);
#endif

	return 0;
}

static int nt35510_spi_resume(struct spi_device *spi)
{
	/* reinitialize the panel */
#if 0
	zeus_panel_power_enable(1);
	nt35510lcd_spi = spi;
	nt35510lcd_spi->mode = SPI_MODE_0;
	nt35510lcd_spi->bits_per_word = 16 ;
	spi_setup(nt35510lcd_spi);

	lcd_poweron();
#endif
	return 0;
}

static struct spi_driver nt35510_spi_driver = {
	.probe    = nt35510_spi_probe,
	.remove   = nt35510_spi_remove,
	.shutdown = nt35510_spi_shutdown,
	.suspend  = nt35510_spi_suspend,
	.resume   = nt35510_spi_resume,
	.driver   = {
		.name   = "nt35510_disp_spi",
		.bus    = &spi_bus_type,
		.owner  = THIS_MODULE,
	},
};

static int __init nt35510_lcd_init(void)
{
   	return spi_register_driver(&nt35510_spi_driver);
}

static void __exit nt35510_lcd_exit(void)
{
	return spi_unregister_driver(&nt35510_spi_driver);
}

module_init(nt35510_lcd_init);
module_exit(nt35510_lcd_exit);
MODULE_LICENSE("GPL");
