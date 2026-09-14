/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QCOM_MDT_LOADER_H__
#define __QCOM_MDT_LOADER_H__

#include <linux/types.h>

#define QCOM_MDT_TYPE_MASK	(7 << 24)
#define QCOM_MDT_TYPE_HASH	(2 << 24)
#define QCOM_MDT_RELOCATABLE	BIT(27)

#define MAX_RSCTABLE_SIZE	SZ_16K

struct device;
struct firmware;
struct qcom_scm_pas_context;
struct resource_table;
struct iommu_domain;

#if IS_ENABLED(CONFIG_QCOM_MDT_LOADER)

ssize_t qcom_mdt_get_size(const struct firmware *fw);
int qcom_mdt_load(struct device *dev, const struct firmware *fw,
		  const char *fw_name, int pas_id, void *mem_region,
		  phys_addr_t mem_phys, size_t mem_size,
		  phys_addr_t *reloc_base);

int qcom_mdt_pas_load(struct qcom_scm_pas_context *ctx, const struct firmware *fw,
		      const char *firmware, void *mem_region,
		      phys_addr_t *reloc_base);

int qcom_mdt_load_no_init(struct device *dev, const struct firmware *fw,
			  const char *fw_name, int pas_id, void *mem_region,
			  phys_addr_t mem_phys, size_t mem_size,
			  phys_addr_t *reloc_base);
void *qcom_mdt_read_metadata(const struct firmware *fw, size_t *data_len,
			     const char *fw_name, struct device *dev);

int qcom_mdt_pas_map_devmem_rscs(struct qcom_scm_pas_context *ctx, struct iommu_domain *domain,
				 void *rsc_table, size_t rsc_size);

void qcom_mdt_pas_unmap_devmem_rscs(struct qcom_scm_pas_context *ctx, struct iommu_domain *domain);

#else /* !IS_ENABLED(CONFIG_QCOM_MDT_LOADER) */

static inline ssize_t qcom_mdt_get_size(const struct firmware *fw)
{
	return -ENODEV;
}

static inline int qcom_mdt_load(struct device *dev, const struct firmware *fw,
				const char *fw_name, int pas_id,
				void *mem_region, phys_addr_t mem_phys,
				size_t mem_size, phys_addr_t *reloc_base)
{
	return -ENODEV;
}

static inline int qcom_mdt_pas_load(struct qcom_scm_pas_context *ctx,
				    const struct firmware *fw, const char *firmware,
				    void *mem_region, phys_addr_t *reloc_base)
{
	return -ENODEV;
}

static inline int qcom_mdt_load_no_init(struct device *dev,
					const struct firmware *fw,
					const char *fw_name, int pas_id,
					void *mem_region, phys_addr_t mem_phys,
					size_t mem_size,
					phys_addr_t *reloc_base)
{
	return -ENODEV;
}

static inline void *qcom_mdt_read_metadata(const struct firmware *fw,
					   size_t *data_len, const char *fw_name,
					   struct device *dev)
{
	return ERR_PTR(-ENODEV);
}

static inline int qcom_mdt_pas_map_devmem_rscs(struct device *dev, bool has_iommu,
					       struct iommu_domain *domain, int pas_id,
					       phys_addr_t rsc_table, size_t rsc_size)
{
	return -ENODEV;
}

void qcom_mdt_pas_unmap_devmem_rscs(bool has_iommu, int pas_id, struct iommu_domain *domain)
{
}
#endif /* !IS_ENABLED(CONFIG_QCOM_MDT_LOADER) */

#endif
