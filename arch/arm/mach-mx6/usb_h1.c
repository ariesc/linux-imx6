/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <mach/arc_otg.h>
#include <mach/hardware.h>
#include "devices-imx6q.h"
#include "usb.h"

static struct clk *usb_phy2_clk;
static struct clk *usb_oh3_clk;
extern int clk_get_usecount(struct clk *clk);
static struct fsl_usb2_platform_data usbh1_config;

static void usbh1_internal_phy_clock_gate(bool on)
{
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY1_BASE_ADDR);
	if (on) {
		__raw_writel(BM_USBPHY_CTRL_CLKGATE, phy_reg + HW_USBPHY_CTRL_CLR);
	} else {
		__raw_writel(BM_USBPHY_CTRL_CLKGATE, phy_reg + HW_USBPHY_CTRL_SET);
	}
}

static int usb_phy_enable(struct fsl_usb2_platform_data *pdata)
{
	u32 tmp;
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY1_BASE_ADDR);
	void __iomem *phy_ctrl;

	/* Stop then Reset */
	UH1_USBCMD &= ~UCMD_RUN_STOP;
	while (UH1_USBCMD & UCMD_RUN_STOP)
		;

	UH1_USBCMD |= UCMD_RESET;
	while ((UH1_USBCMD) & (UCMD_RESET))
		;
	/* Reset USBPHY module */
	phy_ctrl = phy_reg + HW_USBPHY_CTRL;
	tmp = __raw_readl(phy_ctrl);
	tmp |= BM_USBPHY_CTRL_SFTRST;
	__raw_writel(tmp, phy_ctrl);
	udelay(10);

	/* Remove CLKGATE and SFTRST */
	tmp = __raw_readl(phy_ctrl);
	tmp &= ~(BM_USBPHY_CTRL_CLKGATE | BM_USBPHY_CTRL_SFTRST);
	__raw_writel(tmp, phy_ctrl);
	udelay(10);

	/* Power up the PHY */
	__raw_writel(0, phy_reg + HW_USBPHY_PWD);
	/* enable FS/LS device */
	tmp = __raw_readl(phy_reg + HW_USBPHY_CTRL);
	tmp |= (BM_USBPHY_CTRL_ENUTMILEVEL2 | BM_USBPHY_CTRL_ENUTMILEVEL3);
	__raw_writel(tmp, phy_reg + HW_USBPHY_CTRL);

	return 0;
}
static int fsl_usb_host_init_ext(struct platform_device *pdev)
{
	int ret;
	struct clk *usb_clk;
	usb_clk = clk_get(NULL, "usboh3_clk");
	clk_enable(usb_clk);
	usb_oh3_clk = usb_clk;

	usb_clk = clk_get(NULL, "usb_phy2_clk");
	clk_enable(usb_clk);
	usb_phy2_clk = usb_clk;

	ret = fsl_usb_host_init(pdev);
	if (ret) {
		printk(KERN_ERR "host1 init fails......\n");
		return ret;
	}
	usbh1_internal_phy_clock_gate(true);
	usb_phy_enable(pdev->dev.platform_data);

	return 0;
}

static void fsl_usb_host_uninit_ext(struct platform_device *pdev)
{
	struct fsl_usb2_platform_data *pdata = pdev->dev.platform_data;

	fsl_usb_host_uninit(pdata);

	clk_disable(usb_oh3_clk);
	clk_put(usb_oh3_clk);

	clk_disable(usb_phy2_clk);
	clk_put(usb_phy2_clk);

}

static void usbh1_clock_gate(bool on)
{
	pr_debug("%s: on is %d\n", __func__, on);
	if (on) {
		clk_enable(usb_oh3_clk);
		clk_enable(usb_phy2_clk);
	} else {
		clk_disable(usb_phy2_clk);
		clk_disable(usb_oh3_clk);
	}
}

void mx6_set_host1_vbus_func(driver_vbus_func driver_vbus)
{
	usbh1_config.platform_driver_vbus = driver_vbus;
}

static void _wake_up_enable(struct fsl_usb2_platform_data *pdata, bool enable)
{
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY1_BASE_ADDR);

	pr_debug("host1, %s, enable is %d\n", __func__, enable);
	if (enable) {
		__raw_writel(BM_USBPHY_CTRL_ENIDCHG_WKUP | BM_USBPHY_CTRL_ENVBUSCHG_WKUP
				| BM_USBPHY_CTRL_ENDPDMCHG_WKUP
				| BM_USBPHY_CTRL_ENAUTOSET_USBCLKS
				| BM_USBPHY_CTRL_ENAUTOCLR_PHY_PWD
				| BM_USBPHY_CTRL_ENAUTOCLR_CLKGATE
				| BM_USBPHY_CTRL_ENAUTOCLR_USBCLKGATE
				| BM_USBPHY_CTRL_ENAUTO_PWRON_PLL , phy_reg + HW_USBPHY_CTRL_SET);
		USB_H1_CTRL |= (UCTRL_OWIE | UCTRL_WKUP_ID_EN);
	} else {
		USB_H1_CTRL &= ~(UCTRL_OWIE | UCTRL_WKUP_ID_EN);
		/* The interrupt must be disabled for at least 3
		* cycles of the standby clock(32k Hz) , that is 0.094 ms*/
		udelay(100);
	}
}

static void _phy_lowpower_suspend(struct fsl_usb2_platform_data *pdata, bool enable)
{
	u32 tmp;
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY1_BASE_ADDR);
	pr_debug("host1, %s, enable is %d\n", __func__, enable);
	if (enable) {
		UH1_PORTSC1 |= PORTSC_PHCD;

		pr_debug("%s, Poweroff UTMI \n", __func__);

		tmp = (BM_USBPHY_PWD_TXPWDFS
			| BM_USBPHY_PWD_TXPWDIBIAS
			| BM_USBPHY_PWD_TXPWDV2I
			| BM_USBPHY_PWD_RXPWDENV
			| BM_USBPHY_PWD_RXPWD1PT1
			| BM_USBPHY_PWD_RXPWDDIFF
			| BM_USBPHY_PWD_RXPWDRX);
		__raw_writel(tmp, phy_reg + HW_USBPHY_PWD_SET);

		usbh1_internal_phy_clock_gate(false);
	} else {
		if (UH1_PORTSC1 & PORTSC_PHCD) {
			UH1_PORTSC1 &= ~PORTSC_PHCD;
			mdelay(1);
		}
		usbh1_internal_phy_clock_gate(true);
		tmp = (BM_USBPHY_PWD_TXPWDFS
			| BM_USBPHY_PWD_TXPWDIBIAS
			| BM_USBPHY_PWD_TXPWDV2I
			| BM_USBPHY_PWD_RXPWDENV
			| BM_USBPHY_PWD_RXPWD1PT1
			| BM_USBPHY_PWD_RXPWDDIFF
			| BM_USBPHY_PWD_RXPWDRX);
		__raw_writel(tmp, phy_reg + HW_USBPHY_PWD_CLR);

	}
}

static enum usb_wakeup_event _is_usbh1_wakeup(struct fsl_usb2_platform_data *pdata)
{
	u32 wakeup_req = USB_H1_CTRL & UCTRL_OWIR;
	u32 otgsc = UOG_OTGSC;

	if (wakeup_req) {
		pr_debug("the otgsc is 0x%x, usbsts is 0x%x, portsc is 0x%x, wakeup_irq is 0x%x\n", UOG_OTGSC, UOG_USBSTS, UOG_PORTSC1, wakeup_req);
	}
	/* if ID change sts, it is a host wakeup event */
	if (wakeup_req && (otgsc & OTGSC_IS_USB_ID)) {
		pr_debug("otg host ID wakeup\n");
		/* if host ID wakeup, we must clear the b session change sts */
		otgsc &= (~OTGSC_IS_USB_ID);
		return WAKEUP_EVENT_ID;
	}
	if (wakeup_req  && (!(otgsc & OTGSC_STS_USB_ID))) {
		pr_debug("otg host Remote wakeup\n");
		return WAKEUP_EVENT_DPDM;
	}
	return WAKEUP_EVENT_INVALID;
}

static void h1_wakeup_handler(struct fsl_usb2_platform_data *pdata)
{
	_wake_up_enable(pdata, false);
	_phy_lowpower_suspend(pdata, false);
	pdata->wakeup_event = 1;
}

static void usbh1_wakeup_event_clear(void)
{
	void __iomem *phy_reg = MX6_IO_ADDRESS(USB_PHY1_BASE_ADDR);
	u32 wakeup_irq_bits;

	wakeup_irq_bits = BM_USBPHY_CTRL_RESUME_IRQ | BM_USBPHY_CTRL_WAKEUP_IRQ;
	if (__raw_readl(phy_reg + HW_USBPHY_CTRL) && wakeup_irq_bits) {
		/* clear the wakeup interrupt status */
		__raw_writel(wakeup_irq_bits, phy_reg + HW_USBPHY_CTRL_CLR);
	}
}

static struct fsl_usb2_platform_data usbh1_config = {
	.name		= "Host 1",
	.init		= fsl_usb_host_init_ext,
	.exit		= fsl_usb_host_uninit_ext,
	.operating_mode = FSL_USB2_MPH_HOST,
	.phy_mode = FSL_USB2_PHY_UTMI_WIDE,
	.power_budget = 500,	/* 500 mA max power */
	.wake_up_enable = _wake_up_enable,
	.usb_clock_for_pm  = usbh1_clock_gate,
	.phy_lowpower_suspend = _phy_lowpower_suspend,
	.is_wakeup_event = _is_usbh1_wakeup,
	.wakeup_handler = h1_wakeup_handler,
	.transceiver = "utmi",
	.phy_regs = USB_PHY1_BASE_ADDR,
};
static struct fsl_usb2_wakeup_platform_data usbh1_wakeup_config = {
		.name = "USBH1 wakeup",
		.usb_clock_for_pm  = usbh1_clock_gate,
		.usb_pdata = {&usbh1_config, NULL, NULL},
		.usb_wakeup_exhandle = usbh1_wakeup_event_clear,
};

void __init mx6_usb_h1_init(void)
{
	imx6q_add_fsl_ehci_hs(1, &usbh1_config);
	usbh1_config.wakeup_pdata = &usbh1_wakeup_config;
	imx6q_add_fsl_usb2_hs_wakeup(1, &usbh1_wakeup_config);
}
