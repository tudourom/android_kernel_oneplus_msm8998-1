/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
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

#ifndef __HDMI_CONNECTOR_H__
#define __HDMI_CONNECTOR_H__

#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/hdmi.h>

#include "msm_drv.h"
#include "hdmi.xml.h"

#define HDMI_SEC_TO_MS 1000
#define HDMI_MS_TO_US 1000
#define HDMI_SEC_TO_US (HDMI_SEC_TO_MS * HDMI_MS_TO_US)
#define HDMI_KHZ_TO_HZ 1000
#define HDMI_BUSY_WAIT_DELAY_US 100

struct hdmi_phy;
struct hdmi_platform_config;

struct hdmi_audio {
	bool enabled;
	struct hdmi_audio_infoframe infoframe;
	int rate;
};

struct hdmi_hdcp_ctrl;

struct hdmi {
	struct drm_device *dev;
	struct platform_device *pdev;

	const struct hdmi_platform_config *config;

	/* audio state: */
	struct hdmi_audio audio;

	/* video state: */
	bool power_on;
	unsigned long int pixclock;
	unsigned long int actual_pixclock;

	void __iomem *mmio;
	void __iomem *qfprom_mmio;
	void __iomem *hdcp_mmio;
	u32 mmio_len;
	u32 qfprom_mmio_len;
	u32 hdcp_mmio_len;
	phys_addr_t mmio_phy_addr;

	struct regulator **hpd_regs;
	struct regulator **pwr_regs;
	struct clk **hpd_clks;
	struct clk **pwr_clks;

	struct hdmi_phy *phy;
	struct i2c_adapter *i2c;
	struct drm_connector *connector;
	struct drm_bridge *bridge;

	/* the encoder we are hooked to (outside of hdmi block) */
	struct drm_encoder *encoder;

	bool hdmi_mode;               /* are we in hdmi mode? */
	bool is_hdcp_supported;
	int irq;
	void (*ddc_sw_done_cb)(void *data);
	void *sw_done_cb_data;
	struct workqueue_struct *workq;

	struct hdmi_hdcp_ctrl *hdcp_ctrl;
	bool use_hard_timeout;
	int busy_wait_us;
	u32 timeout_count;
	/*
	* spinlock to protect registers shared by different execution
	* REG_HDMI_CTRL
	* REG_HDMI_DDC_ARBITRATION
	* REG_HDMI_HDCP_INT_CTRL
	* REG_HDMI_HPD_CTRL
	*/
	spinlock_t reg_lock;
};

/* platform config data (ie. from DT, or pdata) */
struct hdmi_platform_config {
	struct hdmi_phy *(*phy_init)(struct hdmi *hdmi);
	const char *mmio_name;
	const char *qfprom_mmio_name;
	const char *hdcp_mmio_name;
	/* regulators that need to be on for hpd: */
	const char **hpd_reg_names;
	int hpd_reg_cnt;

	/* regulators that need to be on for screen pwr: */
	const char **pwr_reg_names;
	int pwr_reg_cnt;

	/* clks that need to be on for hpd: */
	const char **hpd_clk_names;
	const long unsigned *hpd_freq;
	int hpd_clk_cnt;

	/* clks that need to be on for screen pwr (ie pixel clk): */
	const char **pwr_clk_names;
	int pwr_clk_cnt;

	/* gpio's: */
	int ddc_clk_gpio, ddc_data_gpio;
	int hpd_gpio, mux_en_gpio;
	int mux_sel_gpio, hpd5v_gpio;
	int mux_lpm_gpio;
};

struct hdmi_i2c_adapter {
	struct i2c_adapter base;
	struct hdmi *hdmi;
	bool sw_done;
	wait_queue_head_t ddc_event;
};

void hdmi_set_mode(struct hdmi *hdmi, bool power_on);

#define to_hdmi_i2c_adapter(x) container_of(x, struct hdmi_i2c_adapter, base)

int ddc_clear_irq(struct hdmi *hdmi);
void init_ddc(struct hdmi *hdmi);

static inline void hdmi_write(struct hdmi *hdmi, u32 reg, u32 data)
{
	msm_writel(data, hdmi->mmio + reg);
}

static inline u32 hdmi_read(struct hdmi *hdmi, u32 reg)
{
	return msm_readl(hdmi->mmio + reg);
}

static inline u32 hdmi_qfprom_read(struct hdmi *hdmi, u32 reg)
{
	return msm_readl(hdmi->qfprom_mmio + reg);
}

/*
 * The phy appears to be different, for example between 8960 and 8x60,
 * so split the phy related functions out and load the correct one at
 * runtime:
 */

struct hdmi_phy_funcs {
	void (*destroy)(struct hdmi_phy *phy);
	void (*powerup)(struct hdmi_phy *phy, unsigned long int pixclock);
	void (*powerdown)(struct hdmi_phy *phy);
};

struct hdmi_phy {
	const struct hdmi_phy_funcs *funcs;
};

struct hdmi_phy *hdmi_phy_8960_init(struct hdmi *hdmi);
struct hdmi_phy *hdmi_phy_8x60_init(struct hdmi *hdmi);
struct hdmi_phy *hdmi_phy_8x74_init(struct hdmi *hdmi);

/*
 * audio:
 */

int hdmi_audio_update(struct hdmi *hdmi);
int hdmi_audio_info_setup(struct hdmi *hdmi, bool enabled,
	uint32_t num_of_channels, uint32_t channel_allocation,
	uint32_t level_shift, bool down_mix);
void hdmi_audio_set_sample_rate(struct hdmi *hdmi, int rate);


/*
 * hdmi bridge:
 */

struct drm_bridge *hdmi_bridge_init(struct hdmi *hdmi);
void hdmi_bridge_destroy(struct drm_bridge *bridge);

/*
 * hdmi connector:
 */

void hdmi_connector_irq(struct drm_connector *connector);
struct drm_connector *hdmi_connector_init(struct hdmi *hdmi);

/*
 * i2c adapter for ddc:
 */

void hdmi_i2c_irq(struct i2c_adapter *i2c);
void hdmi_i2c_destroy(struct i2c_adapter *i2c);
struct i2c_adapter *hdmi_i2c_init(struct hdmi *hdmi);

/*
 * DDC utility functions
 */
int hdmi_ddc_read(struct hdmi *hdmi, u16 addr, u8 offset,
				  u8 *data, u16 data_len, bool self_retry);
int hdmi_ddc_write(struct hdmi *hdmi, u16 addr, u8 offset,
				   u8 *data, u16 data_len, bool self_retry);
/*
 * hdcp
 */
struct hdmi_hdcp_ctrl *hdmi_hdcp_ctrl_init(struct hdmi *hdmi);
void hdmi_hdcp_ctrl_destroy(struct hdmi *hdmi);
void hdmi_hdcp_ctrl_on(struct hdmi_hdcp_ctrl *hdcp_ctrl);
void hdmi_hdcp_ctrl_off(struct hdmi_hdcp_ctrl *hdcp_ctrl);
void hdmi_hdcp_ctrl_irq(struct hdmi_hdcp_ctrl *hdcp_ctrl);

#endif /* __HDMI_CONNECTOR_H__ */
