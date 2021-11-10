/*
 * 
 *
 * Author: Three Five Displays
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <common.h>
#include <linux/compat.h>
#include <linux/err.h>
#include <asm/gpio.h>
#include <asm/arch/mipi_display.h>

#define tfc_d9400mtwv30tc_01_WIDTH_MM	217
#define tfc_d9400mtwv30tc_01_HEIGHT_MM	136

#define RESET_GPIO		17	/* A 17 */
#define RESET_DELAY		100
#define POWER_ON_DELAY		50
#define INIT_DELAY		100
#define FLIP_HORIZONTAL		0
#define FLIP_VERTICAL		0

#define mdelay(a)	udelay(a * 1000)

struct tfc_d9400mtwv30tc_01 {
	struct mipi_dsi_device *dsi;
	int reset_gpio;
	u32 reset_delay;
	u32 power_on_delay;
	u32 init_delay;
	bool flip_horizontal;
	bool flip_vertical;
	bool is_power_on;

	u8 id[3];
	/* This field is tested by functions directly accessing DSI bus before
	 * transfer, transfer is skipped if it is set. In case of transfer
	 * failure or unexpected response the field is set to error value.
	 * Such construct allows to eliminate many checks in higher level
	 * functions.
	 */
	int error;
};

static inline struct tfc_d9400mtwv30tc_01 *dsi_to_tfc_d9400mtwv30tc_01(struct mipi_dsi_device *dsi)
{
	return dsi->ops->private_data;
}

static void tfc_d9400mtwv30tc_01_dcs_write(struct tfc_d9400mtwv30tc_01 *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	ssize_t ret;

	if (ctx->error < 0)
		return;

	ret = dsi->write_buffer(dsi, data, len);
	if (ret < 0) {
		printf("error %zd writing dcs seq: %*ph\n", ret,
		       (int)len, data);
		ctx->error = ret;
	}
}

#define tfc_d9400mtwv30tc_01_dcs_write_seq(ctx, seq...) \
({\
	const u8 d[] = { seq };\
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, "DCS sequence too big for stack");\
	tfc_d9400mtwv30tc_01_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

#define tfc_d9400mtwv30tc_01_dcs_write_seq_static(ctx, seq...) \
({\
	static const u8 d[] = { seq };\
	tfc_d9400mtwv30tc_01_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

static void tfc_d9400mtwv30tc_01_set_sequence(struct tfc_d9400mtwv30tc_01 *ctx)
{
	if (ctx->error != 0)
		return;

	mdelay(18);

	tfc_d9400mtwv30tc_01_dcs_write_seq_static(ctx, 0x11);
	mdelay(120);
	tfc_d9400mtwv30tc_01_dcs_write_seq_static(ctx, 0x29);
	mdelay(50);
}

static int tfc_d9400mtwv30tc_01_power_on(struct tfc_d9400mtwv30tc_01 *ctx)
{
	if (ctx->is_power_on)
		return 0;

	mdelay(ctx->power_on_delay);

	gpio_direction_output(ctx->reset_gpio, 1);
	mdelay(6);
	gpio_set_value(ctx->reset_gpio, 0);
	mdelay(100);
	gpio_set_value(ctx->reset_gpio, 1);
	mdelay(10);
	mdelay(ctx->reset_delay);

	ctx->is_power_on = true;

	return 0;
}

static int tfc_d9400mtwv30tc_01_power_off(struct tfc_d9400mtwv30tc_01 *ctx)
{
	if (!ctx->is_power_on)
		return 0;

	gpio_set_value(ctx->reset_gpio, 0);
	mdelay(6);

	ctx->is_power_on = false;

	return 0;
}

static int tfc_d9400mtwv30tc_01_unprepare(struct mipi_dsi_device *dsi)
{
	struct tfc_d9400mtwv30tc_01 *ctx = dsi_to_tfc_d9400mtwv30tc_01(dsi);
	int ret;

	ret = tfc_d9400mtwv30tc_01_power_off(ctx);
	if (ret)
		return ret;

	return 0;
}

static int tfc_d9400mtwv30tc_01_prepare(struct mipi_dsi_device *dsi)
{
	struct tfc_d9400mtwv30tc_01 *ctx = dsi_to_tfc_d9400mtwv30tc_01(dsi);
	int ret;

	ret = tfc_d9400mtwv30tc_01_power_on(ctx);
	if (ret < 0)
		return ret;

	tfc_d9400mtwv30tc_01_set_sequence(ctx);
	ret = ctx->error;

	if (ret < 0)
		tfc_d9400mtwv30tc_01_unprepare(dsi);

	return ret;
}

static int tfc_d9400mtwv30tc_01_enable(struct mipi_dsi_device *dsi)
{
	struct tfc_d9400mtwv30tc_01 *ctx = dsi_to_tfc_d9400mtwv30tc_01(dsi);

	tfc_d9400mtwv30tc_01_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_ON);
	if (ctx->error != 0)
		return ctx->error;

	return 0;
}

static int tfc_d9400mtwv30tc_01_disable(struct mipi_dsi_device *dsi)
{
	struct tfc_d9400mtwv30tc_01 *ctx = dsi_to_tfc_d9400mtwv30tc_01(dsi);

	tfc_d9400mtwv30tc_01_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	if (ctx->error != 0)
		return ctx->error;

	mdelay(35);

	tfc_d9400mtwv30tc_01_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	if (ctx->error != 0)
		return ctx->error;

	mdelay(125);

	return 0;
}

static struct mipi_panel_ops tfc_d9400mtwv30tc_01_ops = {
	.prepare = tfc_d9400mtwv30tc_01_prepare,
	.unprepare = tfc_d9400mtwv30tc_01_unprepare,
	.enable = tfc_d9400mtwv30tc_01_enable,
	.disable = tfc_d9400mtwv30tc_01_disable,
};

static int tfc_d9400mtwv30tc_01_parse_dt(struct tfc_d9400mtwv30tc_01 *ctx)
{
	ctx->reset_gpio = RESET_GPIO;
	ctx->reset_delay = RESET_DELAY;
	ctx->power_on_delay = POWER_ON_DELAY;
	ctx->init_delay = INIT_DELAY;
	ctx->flip_horizontal = FLIP_HORIZONTAL;
	ctx->flip_vertical = FLIP_VERTICAL;

	return 0;
}

static int tfc_d9400mtwv30tc_01_init(struct mipi_dsi_device *dsi)
{
	struct tfc_d9400mtwv30tc_01 *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dsi = dsi;
	ctx->is_power_on = false;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO
		| MIPI_DSI_MODE_VIDEO_HFP | MIPI_DSI_MODE_VIDEO_HBP
		| MIPI_DSI_MODE_VIDEO_HSA | MIPI_DSI_MODE_VSYNC_FLUSH;

	dsi->ops = &tfc_d9400mtwv30tc_01_ops;
	dsi->ops->private_data = ctx;

	ret = tfc_d9400mtwv30tc_01_parse_dt(ctx);
	if (ret < 0)
		return ret;

	ret = gpio_request(ctx->reset_gpio, "reset-gpio");
	if (ret) {
		printf("fail: tfc_d9400mtwv30tc_01 request reset-gpio !\n");
		return ret;
	}

	return ret;
}

int nx_mipi_dsi_lcd_bind(struct mipi_dsi_device *dsi)
{
	return tfc_d9400mtwv30tc_01_init(dsi);
}
