/* Checks that a VA-API surface really is backed by an exportable DMA-BUF:
 * export it, mmap the returned fd, and confirm the plane layout the driver
 * advertises matches the memory the app can actually reach.
 *
 * Build on target:
 *   gcc -O0 -o va_export_test va_export_test.c -lva -lva-drm
 */
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_drmcommon.h>

#define WIDTH  640
#define HEIGHT 480

#define CHECK(cond, msg)                                  \
	do {                                              \
		if (cond) {                               \
			printf("  ok   %s\n", msg);       \
		} else {                                  \
			printf("  FAIL %s\n", msg);       \
			failures++;                       \
		}                                         \
	} while (0)

int main(void)
{
	int fd, rc, i, failures = 0;
	VADisplay display;
	VAConfigID config;
	VAContextID context;
	VASurfaceID surface;
	VASurfaceAttrib attrs[8];
	unsigned int num_attrs = 8;
	VADRMPRIMESurfaceDescriptor desc;
	int major, minor;

	fd = open("/dev/dri/renderD128", O_RDWR);
	if (fd < 0) { perror("open renderD128"); return 1; }
	display = vaGetDisplayDRM(fd);
	if (!display) { fprintf(stderr, "vaGetDisplayDRM failed\n"); return 1; }

	rc = vaInitialize(display, &major, &minor);
	if (rc != VA_STATUS_SUCCESS) {
		fprintf(stderr, "vaInitialize: %s\n", vaErrorStr(rc));
		return 1;
	}
	printf("driver: %s\n", vaQueryVendorString(display));

	rc = vaCreateConfig(display, VAProfileH264High, VAEntrypointVLD,
			    NULL, 0, &config);
	if (rc != VA_STATUS_SUCCESS) {
		fprintf(stderr, "vaCreateConfig: %s\n", vaErrorStr(rc));
		return 1;
	}

	/* Does the driver advertise DRM_PRIME_2 surfaces? */
	rc = vaQuerySurfaceAttributes(display, config, attrs, &num_attrs);
	if (rc != VA_STATUS_SUCCESS) {
		fprintf(stderr, "vaQuerySurfaceAttributes: %s\n",
			vaErrorStr(rc));
		return 1;
	}
	for (i = 0; i < (int)num_attrs; i++) {
		if (attrs[i].type != VASurfaceAttribMemoryType)
			continue;
		printf("advertised mem types: 0x%08x\n",
		       (unsigned)attrs[i].value.value.i);
		CHECK(attrs[i].value.value.i &
			      VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
		      "advertises DRM_PRIME_2");
	}

	rc = vaCreateContext(display, config, WIDTH, HEIGHT, VA_PROGRESSIVE,
			     NULL, 0, &context);
	if (rc != VA_STATUS_SUCCESS) {
		fprintf(stderr, "vaCreateContext: %s\n", vaErrorStr(rc));
		return 1;
	}

	rc = vaCreateSurfaces(display, VA_RT_FORMAT_YUV420, WIDTH, HEIGHT,
			      &surface, 1, NULL, 0);
	if (rc != VA_STATUS_SUCCESS) {
		fprintf(stderr, "vaCreateSurfaces: %s\n", vaErrorStr(rc));
		return 1;
	}

	memset(&desc, 0, sizeof(desc));
	rc = vaExportSurfaceHandle(display, surface,
				   VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
				   VA_EXPORT_SURFACE_READ_WRITE |
				   VA_EXPORT_SURFACE_COMPOSED_LAYERS,
				   &desc);
	printf("vaExportSurfaceHandle -> %s\n", vaErrorStr(rc));
	if (rc != VA_STATUS_SUCCESS) {
		fprintf(stderr, "FAIL: cannot export surface\n");
		return 1;
	}

	printf("descriptor: fourcc=0x%08x %ux%u objects=%u layers=%u\n",
	       desc.fourcc, desc.width, desc.height, desc.num_objects,
	       desc.num_layers);

	CHECK(desc.fourcc == VA_FOURCC_NV12, "fourcc is NV12");
	CHECK(desc.width == WIDTH && desc.height == HEIGHT, "dimensions match");
	CHECK(desc.num_objects == 1, "one DRM object");
	CHECK(desc.num_layers == 1, "one layer");
	CHECK(desc.objects[0].fd >= 0, "object has a valid fd");
	CHECK(desc.layers[0].num_planes == 2, "NV12 layer has two planes");
	CHECK(desc.layers[0].pitch[0] == WIDTH, "plane 0 pitch == width");
	CHECK(desc.layers[0].offset[1] == (uint32_t)(WIDTH * HEIGHT),
	      "plane 1 offset == luma size");
	CHECK(desc.layers[0].drm_format == 0x3231564e /* NV12 */,
	      "layer drm_format is DRM_FORMAT_NV12");

	/* The point of the export: the app can map it. */
	{
		size_t size = desc.objects[0].size;
		uint8_t *map = mmap(NULL, size, PROT_READ | PROT_WRITE,
				    MAP_SHARED, desc.objects[0].fd, 0);

		CHECK(map != MAP_FAILED, "exported fd is mmap-able");
		if (map != MAP_FAILED) {
			size_t luma = (size_t)WIDTH * HEIGHT;

			/* Plane 1 must live inside the mapping. */
			CHECK(luma + luma / 2 <= size,
			      "mapping covers both planes");
			/* The driver zero-fills new surfaces. */
			map[luma] = 0xAB;
			CHECK(map[luma] == 0xAB, "plane 1 is writable");
			munmap(map, size);
		}
	}

	for (i = 0; i < desc.num_objects; i++)
		close(desc.objects[i].fd);

	/* VLC's GL interop asks for neither layout flag and then refuses any
	 * layer holding more than one plane, so that request must come back as
	 * one layer per plane.  It is the layout that was wrong before and made
	 * every VLC frame land in an empty texture (a flat green picture). */
	{
		VADRMPRIMESurfaceDescriptor sep;

		memset(&sep, 0, sizeof(sep));
		rc = vaExportSurfaceHandle(display, surface,
					   VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
					   0, &sep);
		printf("vaExportSurfaceHandle(flags=0) -> %s\n",
		       vaErrorStr(rc));
		CHECK(rc == VA_STATUS_SUCCESS, "flags=0 export succeeds");
		if (rc != VA_STATUS_SUCCESS)
			return 1;
		CHECK(sep.num_objects == 1, "flags=0: one DRM object");
		CHECK(sep.num_layers == 2, "flags=0: two separate layers");
		CHECK(sep.layers[0].num_planes == 1,
		      "flags=0: layer 0 has one plane");
		CHECK(sep.layers[1].num_planes == 1,
		      "flags=0: layer 1 has one plane");
		CHECK(sep.layers[0].offset[0] == 0,
		      "flags=0: luma offset 0");
		CHECK(sep.layers[1].offset[0] == (uint32_t)(WIDTH * HEIGHT),
		      "flags=0: chroma offset == luma size");
		CHECK(sep.layers[0].pitch[0] == WIDTH,
		      "flags=0: luma pitch == width");
		CHECK(sep.layers[1].pitch[0] == WIDTH,
		      "flags=0: chroma pitch == width");
		CHECK(sep.layers[0].drm_format == 0x20203852 /* R8 */,
		      "flags=0: layer 0 is DRM_FORMAT_R8");
		CHECK(sep.layers[1].drm_format == 0x38385247 /* GR88 */,
		      "flags=0: layer 1 is DRM_FORMAT_GR88");
		for (i = 0; i < sep.num_objects; i++)
			close(sep.objects[i].fd);
	}

	printf("%s (%d failure%s)\n", failures ? "RESULT: FAIL" : "RESULT: PASS",
	       failures, failures == 1 ? "" : "s");

	vaDestroySurfaces(display, &surface, 1);
	vaDestroyContext(display, context);
	vaDestroyConfig(display, config);
	vaTerminate(display);
	close(fd);
	return failures ? 1 : 0;
}
