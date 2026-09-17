// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/fb.h>

#include <drm/drm_drv.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_prime.h>

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_kms.h"

static bool fbdev = true;
MODULE_PARM_DESC(fbdev, "Enable fbdev compat layer");
module_param(fbdev, bool, 0600);

/*
 * fbdev funcs, to implement legacy fbdev interface on top of drm driver
 */

FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS(msm_fbdev,
				   drm_fb_helper_damage_range,
				   drm_fb_helper_damage_area)

static int msm_fbdev_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *helper = (struct drm_fb_helper *)info->par;
	struct drm_gem_object *bo = msm_framebuffer_bo(helper->fb, 0);

	return drm_gem_prime_mmap(bo, vma);
}

static void msm_fbdev_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *helper = (struct drm_fb_helper *)info->par;
	struct drm_framebuffer *fb = helper->fb;
	struct drm_gem_object *bo = msm_framebuffer_bo(fb, 0);

	DBG();

	drm_fb_helper_fini(helper);

	/* this will free the backing object */
	msm_gem_put_vaddr(bo);
	drm_framebuffer_remove(fb);

	drm_client_release(&helper->client);
	drm_fb_helper_unprepare(helper);
	kfree(helper);
}

static const struct fb_ops msm_fb_ops = {
	/*
	 * 刻意不设 .owner (Quectel Pi H1, 自 Android16 移植):
	 *
	 * 内核启动 logo 的三道闸门都只看 info->fbops->owner 是否为空:
	 *   drivers/video/fbdev/core/fbmem.c:620  fb_prepare_logo()
	 *   drivers/video/fbdev/core/fbmem.c:464  fb_show_logo_line()
	 *   drivers/video/fbdev/core/fbcon.c:565  fbcon_prepare_logo()
	 * .owner 非空 -> 启动 logo 一帧都不画。本板要求开机在 DSI 面板上显示
	 * Quectel 标识 (logo 资产与生成脚本见 drivers/video/logo/)。
	 *
	 * !! 本目录 (msm_default/) 才是编出 msm.ko 的那份:
	 *      drivers/gpu/drm/Makefile:170  obj-$(CONFIG_DRM_MSM) += msm_default/
	 *    msm/ 编出的是 msm_display.ko (Makefile:169), 虽然也装进了
	 *    /lib/modules/, 但被 /etc/modprobe.d/blacklist-msm_display.conf 拉黑、
	 *    从不加载。两处都改, 以防将来放开黑名单后行为不一致。
	 *
	 * 代价: 打开 /dev/fb0 时不再 try_module_get(owner) 钉住本模块。
	 * 本产品 msm.ko 从不卸载 (无 modprobe -r, 无可卸热插拔路径), 故无实际风险;
	 * 且 try_module_get()/module_put() 对 NULL 都有保护, fb_chrdev.c / fbcon.c
	 * 里那几处调用不会因此解引用空指针。
	 * 若将来要让 msm.ko 可卸载, 必须放弃启动 logo, 不要改回 .owner。
	 */
	__FB_DEFAULT_DEFERRED_OPS_RDWR(msm_fbdev),
	DRM_FB_HELPER_DEFAULT_OPS,
	__FB_DEFAULT_DEFERRED_OPS_DRAW(msm_fbdev),
	.fb_mmap = msm_fbdev_mmap,
	.fb_destroy = msm_fbdev_fb_destroy,
};

static int msm_fbdev_create(struct drm_fb_helper *helper,
		struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = helper->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_framebuffer *fb = NULL;
	struct drm_gem_object *bo;
	struct fb_info *fbi = NULL;
	uint64_t paddr;
	uint32_t format;
	int ret, pitch;

	format = drm_mode_legacy_fb_format(sizes->surface_bpp, sizes->surface_depth);

	DBG("create fbdev: %dx%d@%d (%dx%d)", sizes->surface_width,
			sizes->surface_height, sizes->surface_bpp,
			sizes->fb_width, sizes->fb_height);

	pitch = align_pitch(sizes->surface_width, sizes->surface_bpp);
	fb = msm_alloc_stolen_fb(dev, sizes->surface_width,
			sizes->surface_height, pitch, format);

	if (IS_ERR(fb)) {
		DRM_DEV_ERROR(dev->dev, "failed to allocate fb\n");
		return PTR_ERR(fb);
	}

	bo = msm_framebuffer_bo(fb, 0);

	/*
	 * NOTE: if we can be guaranteed to be able to map buffer
	 * in panic (ie. lock-safe, etc) we could avoid pinning the
	 * buffer now:
	 */
	ret = msm_gem_get_and_pin_iova(bo, priv->kms->aspace, &paddr);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "failed to get buffer obj iova: %d\n", ret);
		goto fail;
	}

	fbi = drm_fb_helper_alloc_info(helper);
	if (IS_ERR(fbi)) {
		DRM_DEV_ERROR(dev->dev, "failed to allocate fb info\n");
		ret = PTR_ERR(fbi);
		goto fail;
	}

	DBG("fbi=%p, dev=%p", fbi, dev);

	helper->fb = fb;

	fbi->fbops = &msm_fb_ops;

	drm_fb_helper_fill_info(fbi, helper, sizes);

	fbi->screen_buffer = msm_gem_get_vaddr(bo);
	if (IS_ERR(fbi->screen_buffer)) {
		ret = PTR_ERR(fbi->screen_buffer);
		goto fail;
	}
	fbi->screen_size = bo->size;
	fbi->fix.smem_start = paddr;
	fbi->fix.smem_len = bo->size;

	DBG("par=%p, %dx%d", fbi->par, fbi->var.xres, fbi->var.yres);
	DBG("allocated %dx%d fb", fb->width, fb->height);

	return 0;

fail:
	drm_framebuffer_remove(fb);
	return ret;
}

static int msm_fbdev_fb_dirty(struct drm_fb_helper *helper,
			      struct drm_clip_rect *clip)
{
	struct drm_device *dev = helper->dev;
	int ret;

	/* Call damage handlers only if necessary */
	if (!(clip->x1 < clip->x2 && clip->y1 < clip->y2))
		return 0;

	if (helper->fb->funcs->dirty) {
		ret = helper->fb->funcs->dirty(helper->fb, NULL, 0, 0, clip, 1);
		if (drm_WARN_ONCE(dev, ret, "Dirty helper failed: ret=%d\n", ret))
			return ret;
	}

	return 0;
}

static const struct drm_fb_helper_funcs msm_fb_helper_funcs = {
	.fb_probe = msm_fbdev_create,
	.fb_dirty = msm_fbdev_fb_dirty,
};

/*
 * struct drm_client
 */

static void msm_fbdev_client_unregister(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);

	if (fb_helper->info) {
		drm_fb_helper_unregister_info(fb_helper);
	} else {
		drm_client_release(&fb_helper->client);
		drm_fb_helper_unprepare(fb_helper);
		kfree(fb_helper);
	}
}

static int msm_fbdev_client_restore(struct drm_client_dev *client)
{
	drm_fb_helper_lastclose(client->dev);

	return 0;
}

static int msm_fbdev_client_hotplug(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);
	struct drm_device *dev = client->dev;
	int ret;

	if (dev->fb_helper)
		return drm_fb_helper_hotplug_event(dev->fb_helper);

	ret = drm_fb_helper_init(dev, fb_helper);
	if (ret)
		goto err_drm_err;

	if (!drm_drv_uses_atomic_modeset(dev))
		drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(fb_helper);
	if (ret)
		goto err_drm_fb_helper_fini;

	return 0;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(fb_helper);
err_drm_err:
	drm_err(dev, "Failed to setup fbdev emulation (ret=%d)\n", ret);
	return ret;
}

static const struct drm_client_funcs msm_fbdev_client_funcs = {
	.owner		= THIS_MODULE,
	.unregister	= msm_fbdev_client_unregister,
	.restore	= msm_fbdev_client_restore,
	.hotplug	= msm_fbdev_client_hotplug,
};

/* initialize fbdev helper */
void msm_fbdev_setup(struct drm_device *dev)
{
	struct drm_fb_helper *helper;
	int ret;

	if (!fbdev)
		return;

	drm_WARN(dev, !dev->registered, "Device has not been registered.\n");
	drm_WARN(dev, dev->fb_helper, "fb_helper is already set!\n");

	helper = kzalloc(sizeof(*helper), GFP_KERNEL);
	if (!helper)
		return;
	drm_fb_helper_prepare(dev, helper, 32, &msm_fb_helper_funcs);

	ret = drm_client_init(dev, &helper->client, "fbdev", &msm_fbdev_client_funcs);
	if (ret) {
		drm_err(dev, "Failed to register client: %d\n", ret);
		goto err_drm_fb_helper_unprepare;
	}

	drm_client_register(&helper->client);

	return;

err_drm_fb_helper_unprepare:
	drm_fb_helper_unprepare(helper);
	kfree(helper);
}
