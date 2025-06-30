#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct buffer_object {
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t handle;
	uint32_t size;
	uint32_t *vaddr;
	uint32_t fb_id;
};

#define BUF_CNT 4
struct buffer_object buf[BUF_CNT];
static int terminate;

static int modeset_create_fb(int fd, struct buffer_object *bo, uint32_t color)
{
	struct drm_mode_create_dumb create = {};
 	struct drm_mode_map_dumb map = {};
	int i;

	create.width = bo->width;
	create.height = bo->height;
	create.bpp = 32;
	drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);

	bo->pitch = create.pitch;
	bo->size = create.size;
	bo->handle = create.handle;
	drmModeAddFB(fd, bo->width, bo->height, 24, 32, bo->pitch,
			   bo->handle, &bo->fb_id);

	map.handle = create.handle;
	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);

	bo->vaddr = mmap(0, create.size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, map.offset);

	for (i = 0; i < (bo->size / 4); i++)
		bo->vaddr[i] = color;

	return 0;
}

static void modeset_destroy_fb(int fd, struct buffer_object *bo)
{
	struct drm_mode_destroy_dumb destroy = {};

	drmModeRmFB(fd, bo->fb_id);

	munmap(bo->vaddr, bo->size);

	destroy.handle = bo->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}

static uint32_t cur_sequence = 0;
static void modeset_page_flip_handler(int fd,
	unsigned int sequence,
	unsigned int tv_sec,
	unsigned int tv_usec,
	void *user_data)
{
	uint32_t crtc_id = *(uint32_t *)user_data;
	int ret;

	ret = drmModePageFlip(fd, crtc_id, buf[((cur_sequence++)/32) % BUF_CNT].fb_id, DRM_MODE_PAGE_FLIP_EVENT, user_data);
	if (ret)
		printf("drmModePageFlip() = %d\n", ret);
}

static void sigint_handler(int arg)
{
	terminate = 1;
}
static const char *connection_state(int state) {
	const char *ret = "";
	switch(state) {
		case DRM_MODE_CONNECTED: ret = "CONNECTED"; break;
		case DRM_MODE_DISCONNECTED: ret = "DISCONNECTED"; break;
		case DRM_MODE_UNKNOWNCONNECTION: ret = "UNKNOWNCONNECTION"; break;
		default: break;
	}

	return ret;
}

static const char *encoder_type(int type) {
	const char *ret = "";
	switch(type) {
		case DRM_MODE_ENCODER_NONE: ret = "NONE"; break;
		case DRM_MODE_ENCODER_DAC: ret = "DAC"; break;
		case DRM_MODE_ENCODER_TMDS: ret = "TMDS"; break;
		case DRM_MODE_ENCODER_LVDS: ret = "LVDS"; break;
		case DRM_MODE_ENCODER_TVDAC: ret = "TVDAC"; break;
		case DRM_MODE_ENCODER_VIRTUAL: ret = "VIRTUAL"; break;
		case DRM_MODE_ENCODER_DSI: ret = "DSI"; break;
		case DRM_MODE_ENCODER_DPI: ret = "DPI"; break;
		default: break;
	}

	return ret;
}

static const char *connector_type(int type) {
	const char *ret = "";
	switch(type) {
		case DRM_MODE_CONNECTOR_Unknown: ret = "Unknown"; break;
		case DRM_MODE_CONNECTOR_VGA: ret = "VGA"; break;
		case DRM_MODE_CONNECTOR_DVII: ret = "DVII"; break;
		case DRM_MODE_CONNECTOR_DVID: ret = "DVID"; break;
		case DRM_MODE_CONNECTOR_DVIA: ret = "DVIA"; break;
		case DRM_MODE_CONNECTOR_Composite: ret = "Composite"; break;
		case DRM_MODE_CONNECTOR_SVIDEO: ret = "SVIDEO"; break;
		case DRM_MODE_CONNECTOR_LVDS: ret = "LVDS"; break;
		case DRM_MODE_CONNECTOR_Component: ret = "Component"; break;
		case DRM_MODE_CONNECTOR_9PinDIN: ret = "9PinDIN"; break;
		case DRM_MODE_CONNECTOR_DisplayPort: ret = "DisplayPort"; break;
		case DRM_MODE_CONNECTOR_HDMIA: ret = "HDMIA"; break;
		case DRM_MODE_CONNECTOR_HDMIB: ret = "HDMIB"; break;
		case DRM_MODE_CONNECTOR_TV: ret = "TV"; break;
		case DRM_MODE_CONNECTOR_eDP: ret = "eDP"; break;
		case DRM_MODE_CONNECTOR_VIRTUAL: ret = "VIRTUAL"; break;
		case DRM_MODE_CONNECTOR_DSI: ret = "DSI"; break;
		case DRM_MODE_CONNECTOR_DPI: ret = "DPI"; break;
		case DRM_MODE_CONNECTOR_WRITEBACK: ret = "WRITEBACK"; break;
		case DRM_MODE_CONNECTOR_SPI: ret = "SPI"; break;
		case DRM_MODE_CONNECTOR_USB: ret = "USB"; break;
		default: break;
	}

	return ret;
}
static void drm_print(int fd, const drmModeRes *res, const drmModePlaneRes *plane_res) {
	int i, j;
	printf("count_fbs=%d [", res->count_fbs);
	for (i = 0; i < res->count_fbs; i++)
		printf(" %d", res->fbs[i]);
	printf(" ]\n");

	printf("count_crtcs=%d\n", res->count_crtcs);
	for (i = 0; i < res->count_crtcs; i++) {
		drmModeCrtc *crtc = drmModeGetCrtc(fd, res->crtcs[i]);
		printf("\t crtc_id=%u\n", crtc->crtc_id);
		printf("\t\t buffer_id=%u, x=%u, y=%u, width=%u, height=%u, mode_valid=%u\n",
			crtc->buffer_id, crtc->x, crtc->y, crtc->width, crtc->height, crtc->mode_valid);
		drmModeFreeCrtc(crtc);
	}

	printf("count_connectors=%d\n", res->count_connectors);
	for (i = 0; i < res->count_connectors; i++) {
		drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
		printf("\t connector_id=%u\n", conn->connector_id);
		printf("\t\t encoder_id=%u\n", conn->encoder_id);
		printf("\t\t connector_type=%u (%s), connector_type_id=%u, connection=%d (%s)\n",
			conn->connector_type, connector_type(conn->connector_type),
			conn->connector_type_id, conn->connection, connection_state(conn->connection));
		printf("\t\t mmWidth=%d, mmHeight=%u, subpixel=%d [",
			conn->mmWidth, conn->mmHeight, conn->subpixel);
		printf("\t\t count_count_modes=%d, count_props=%u, count_encoders=%d [",
			conn->count_modes, conn->count_props, conn->count_encoders);
		for (j = 0; j < conn->count_encoders; j++)
			printf(" %u", conn->encoders[j]);
		printf(" ]\n");
		drmModeFreeConnector(conn);
	}

	printf("count_encoders=%d\n", res->count_encoders);
	for (i = 0; i < res->count_encoders; i++) {
		drmModeEncoder *encoder = drmModeGetEncoder(fd, res->encoders[i]);
		printf("\t encoder_id=%u\n", encoder->encoder_id);
		printf("\t\t encoder_type=%u (%s)\n", encoder->encoder_type, encoder_type(encoder->encoder_type));
		printf("\t\t possible_crtcs=%u, possible_clones=%u\n", encoder->possible_crtcs, encoder->possible_clones);
		drmModeFreeEncoder(encoder);
	}

	printf("min_width=%u min_height=%u, max_width=%u max_height=%u\n",
		res->min_width, res->min_height, res->max_width, res->max_height);
	printf("count_planes = %u\n", plane_res->count_planes);

}

int main(int argc, char **argv)
{
	int fd, i, ret = -1;
	drmEventContext ev = {};
	drmModeConnector *conn;
	drmModeRes *res;
	drmModePlaneRes *plane_res;
	uint32_t conn_id = 0;
	uint32_t crtc_id = 0;
	uint32_t plane_id;

	signal(SIGINT, sigint_handler);
	ev.version = DRM_EVENT_CONTEXT_VERSION;
	ev.page_flip_handler = modeset_page_flip_handler;

	if (argc > 2) {
		crtc_id = atoi(argv[1]);
		conn_id = atoi(argv[2]);
	}

	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	res = drmModeGetResources(fd);
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	plane_res = drmModeGetPlaneResources(fd);
	plane_id = plane_res->planes[0];
	drm_print(fd, res, plane_res);

	for (i = 0; i < res->count_crtcs; i++) {
		if (crtc_id == res->crtcs[i])
			break;
	}
	if (i == res->count_crtcs) {
		printf("crtc_id=%u not found\n", crtc_id);
		crtc_id = res->crtcs[0];
		printf("crtc_id=%u will be used\n", crtc_id);
	}

	for (i = 0; i < res->count_connectors; i++) {
		if (conn_id == res->connectors[i])
			break;
	}
	if (i == res->count_connectors) {
		printf("conn_id=%u not found\n", conn_id);
		printf("usage: %s crtc_id conn_id\n", argv[0]);
		goto _out;
	}

	conn = drmModeGetConnector(fd, conn_id);
	if (!conn || !conn->count_modes || conn->connection != DRM_MODE_CONNECTED) {
		printf("drmModeGetConnector fail or count_modes is 0 or connection is 0\n");
		goto _out;
	}

	printf("conn_id = %u, res = %x x %x \n", conn_id, conn->modes[0].hdisplay, conn->modes[0].vdisplay);
	for (i = 0; i < BUF_CNT; i++) {
		buf[i].width = conn->modes[0].hdisplay;
		buf[i].height = conn->modes[0].vdisplay;
		modeset_create_fb(fd, &buf[i], 0xff<<(i*8));
	}

	ret = drmModeSetCrtc(fd, crtc_id, buf[0].fb_id,
			0, 0, &conn_id, 1, &conn->modes[0]);
	printf("drmModeSetCrtc(crtc_id=%u, conn_id=%u) = %d\n", crtc_id, conn_id, ret);
	if (ret) {
		if (system("fuser /dev/dri/card0")) {};
		goto _out;
	}

	ret = drmModePageFlip(fd, crtc_id, buf[0].fb_id,
			DRM_MODE_PAGE_FLIP_EVENT, &crtc_id);
	printf("drmModePageFlip() = %d\n", ret);
	if (ret) {
		goto _out;
	}

	while (!terminate) {
		drmHandleEvent(fd, &ev);
	}

_out:
	for (i = BUF_CNT; i > 0; i--)
		if (buf[BUF_CNT - 1].vaddr)
			modeset_destroy_fb(fd, &buf[BUF_CNT - 1]);
	drmModeFreePlaneResources(plane_res);
	drmModeFreeResources(res);
	close(fd);
	return ret;
}
