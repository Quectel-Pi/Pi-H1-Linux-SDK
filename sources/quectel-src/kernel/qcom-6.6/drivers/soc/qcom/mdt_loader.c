// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Peripheral Image Loader
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2015 Sony Mobile Communications Inc
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/elf.h>
#include <linux/firmware.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/remoteproc.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/soc/qcom/mdt_loader.h>

#define RSC_TABLE_HASH_BITS	     5  // 32 buckets

DEFINE_HASHTABLE(qcom_pas_rsc_table_map, RSC_TABLE_HASH_BITS);

struct qcom_pas_devmem_rsc {
	struct fw_rsc_devmem *devmem;
	struct list_head node;
};

struct qcom_pas_rsc_table_info {
	struct resource_table *rsc_table;
	struct list_head devmem_list;
	struct hlist_node hnode;
	int pas_id;
};

static bool mdt_header_valid(const struct firmware *fw)
{
	const struct elf32_hdr *ehdr;
	size_t phend;
	size_t shend;

	if (fw->size < sizeof(*ehdr))
		return false;

	ehdr = (struct elf32_hdr *)fw->data;

	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG))
		return false;

	if (ehdr->e_phentsize != sizeof(struct elf32_phdr))
		return false;

	phend = size_add(size_mul(sizeof(struct elf32_phdr), ehdr->e_phnum), ehdr->e_phoff);
	if (phend > fw->size)
		return false;

	if (ehdr->e_shentsize || ehdr->e_shnum) {
		if (ehdr->e_shentsize != sizeof(struct elf32_shdr))
			return false;

		shend = size_add(size_mul(sizeof(struct elf32_shdr), ehdr->e_shnum), ehdr->e_shoff);
		if (shend > fw->size)
			return false;
	}

	return true;
}

static bool mdt_phdr_valid(const struct elf32_phdr *phdr)
{
	if (phdr->p_type != PT_LOAD)
		return false;

	if ((phdr->p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH)
		return false;

	if (!phdr->p_memsz)
		return false;

	return true;
}

static ssize_t mdt_load_split_segment(void *ptr, const struct elf32_phdr *phdrs,
				      unsigned int segment, const char *fw_name,
				      struct device *dev)
{
	const struct elf32_phdr *phdr = &phdrs[segment];
	const struct firmware *seg_fw;
	char *seg_name;
	ssize_t ret;

	if (strlen(fw_name) < 4)
		return -EINVAL;

	seg_name = kstrdup(fw_name, GFP_KERNEL);
	if (!seg_name)
		return -ENOMEM;

	sprintf(seg_name + strlen(fw_name) - 3, "b%02d", segment);
	ret = request_firmware_into_buf(&seg_fw, seg_name, dev,
					ptr, phdr->p_filesz);
	if (ret) {
		dev_err(dev, "error %zd loading %s\n", ret, seg_name);
		kfree(seg_name);
		return ret;
	}

	if (seg_fw->size != phdr->p_filesz) {
		dev_err(dev,
			"failed to load segment %d from truncated file %s\n",
			segment, seg_name);
		ret = -EINVAL;
	}

	release_firmware(seg_fw);
	kfree(seg_name);

	return ret;
}

/**
 * qcom_mdt_get_size() - acquire size of the memory region needed to load mdt
 * @fw:		firmware object for the mdt file
 *
 * Returns size of the loaded firmware blob, or -EINVAL on failure.
 */
ssize_t qcom_mdt_get_size(const struct firmware *fw)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct elf32_hdr *ehdr;
	phys_addr_t min_addr = PHYS_ADDR_MAX;
	phys_addr_t max_addr = 0;
	int i;

	if (!mdt_header_valid(fw))
		return -EINVAL;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(fw->data + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (!mdt_phdr_valid(phdr))
			continue;

		if (phdr->p_paddr < min_addr)
			min_addr = phdr->p_paddr;

		if (phdr->p_paddr + phdr->p_memsz > max_addr)
			max_addr = ALIGN(phdr->p_paddr + phdr->p_memsz, SZ_4K);
	}

	return min_addr < max_addr ? max_addr - min_addr : -EINVAL;
}
EXPORT_SYMBOL_GPL(qcom_mdt_get_size);

/**
 * qcom_mdt_read_metadata() - read header and metadata from mdt or mbn
 * @fw:		firmware of mdt header or mbn
 * @data_len:	length of the read metadata blob
 * @fw_name:	name of the firmware, for construction of segment file names
 * @dev:	device handle to associate resources with
 *
 * The mechanism that performs the authentication of the loading firmware
 * expects an ELF header directly followed by the segment of hashes, with no
 * padding inbetween. This function allocates a chunk of memory for this pair
 * and copy the two pieces into the buffer.
 *
 * In the case of split firmware the hash is found directly following the ELF
 * header, rather than at p_offset described by the second program header.
 *
 * The caller is responsible to free (kfree()) the returned pointer.
 *
 * Return: pointer to data, or ERR_PTR()
 */
void *qcom_mdt_read_metadata(const struct firmware *fw, size_t *data_len,
			     const char *fw_name, struct device *dev)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_hdr *ehdr;
	unsigned int hash_segment = 0;
	size_t hash_offset;
	size_t hash_size;
	size_t ehdr_size;
	unsigned int i;
	ssize_t ret;
	void *data;

	if (!mdt_header_valid(fw))
		return ERR_PTR(-EINVAL);

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(fw->data + ehdr->e_phoff);

	if (ehdr->e_phnum < 2)
		return ERR_PTR(-EINVAL);

	if (phdrs[0].p_type == PT_LOAD)
		return ERR_PTR(-EINVAL);

	if (((size_t)(phdrs + ehdr->e_phnum)) > ((size_t)ehdr + fw->size)) {
		dev_err(dev, "Invalid phdrs access: %s\n", fw_name);
		return ERR_PTR(-EINVAL);
	}

	for (i = 1; i < ehdr->e_phnum; i++) {
		if ((phdrs[i].p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH) {
			hash_segment = i;
			break;
		}
	}

	if (!hash_segment) {
		dev_err(dev, "no hash segment found in %s\n", fw_name);
		return ERR_PTR(-EINVAL);
	}

	ehdr_size = phdrs[0].p_filesz;
	hash_size = phdrs[hash_segment].p_filesz;

	data = kmalloc(ehdr_size + hash_size, GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	/* Copy ELF header */
	memcpy(data, fw->data, ehdr_size);

	if (ehdr_size + hash_size == fw->size) {
		/* Firmware is split and hash is packed following the ELF header */
		hash_offset = phdrs[0].p_filesz;
		memcpy(data + ehdr_size, fw->data + hash_offset, hash_size);
	} else if (phdrs[hash_segment].p_offset + hash_size <= fw->size) {
		/* Hash is in its own segment, but within the loaded file */
		hash_offset = phdrs[hash_segment].p_offset;
		memcpy(data + ehdr_size, fw->data + hash_offset, hash_size);
	} else {
		/* Hash is in its own segment, beyond the loaded file */
		ret = mdt_load_split_segment(data + ehdr_size, phdrs, hash_segment, fw_name, dev);
		if (ret) {
			kfree(data);
			return ERR_PTR(ret);
		}
	}

	*data_len = ehdr_size + hash_size;

	return data;
}
EXPORT_SYMBOL_GPL(qcom_mdt_read_metadata);

/**
 * qcom_mdt_pas_init() - initialize PAS region for firmware loading
 * @dev:	device handle to associate resources with
 * @fw:		firmware object for the mdt file
 * @fw_name:	name of the firmware, for construction of segment file names
 * @pas_id:	PAS identifier
 * @mem_phys:	physical address of allocated memory region
 * @ctx:	PAS context, ctx->metadata to be released by caller
 *
 * Returns 0 on success, negative errno otherwise.
 */
static int __qcom_mdt_pas_init(struct device *dev, const struct firmware *fw,
			       const char *fw_name, int pas_id, phys_addr_t mem_phys,
			       struct qcom_scm_pas_context *ctx)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct elf32_hdr *ehdr;
	phys_addr_t min_addr = PHYS_ADDR_MAX;
	phys_addr_t max_addr = 0;
	bool relocate = false;
	size_t metadata_len;
	void *metadata;
	int ret;
	int i;

	if (!mdt_header_valid(fw))
		return -EINVAL;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(fw->data + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (!mdt_phdr_valid(phdr))
			continue;

		if (phdr->p_flags & QCOM_MDT_RELOCATABLE)
			relocate = true;

		if (phdr->p_paddr < min_addr)
			min_addr = phdr->p_paddr;

		if (phdr->p_paddr + phdr->p_memsz > max_addr)
			max_addr = ALIGN(phdr->p_paddr + phdr->p_memsz, SZ_4K);
	}

	metadata = qcom_mdt_read_metadata(fw, &metadata_len, fw_name, dev);
	if (IS_ERR(metadata)) {
		ret = PTR_ERR(metadata);
		dev_err(dev, "error %d reading firmware %s metadata\n", ret, fw_name);
		goto out;
	}

	ret = qcom_scm_pas_init_image(pas_id, metadata, metadata_len, ctx);
	kfree(metadata);
	if (ret) {
		/* Invalid firmware metadata */
		dev_err(dev, "error %d initializing firmware %s\n", ret, fw_name);
		goto out;
	}

	if (relocate) {
		ret = qcom_scm_pas_mem_setup(pas_id, mem_phys, max_addr - min_addr);
		if (ret) {
			/* Unable to set up relocation */
			dev_err(dev, "error %d setting up firmware %s\n", ret, fw_name);
			goto out;
		}
	}

out:
	return ret;
}

static bool qcom_mdt_bins_are_split(const struct firmware *fw, const char *fw_name)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_hdr *ehdr;
	uint64_t seg_start, seg_end;
	int i;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(fw->data + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		/*
		 * The size of the MDT file is not padded to include any
		 * zero-sized segments at the end. Ignore these, as they should
		 * not affect the decision about image being split or not.
		 */
		if (!phdrs[i].p_filesz)
			continue;

		seg_start = phdrs[i].p_offset;
		seg_end = phdrs[i].p_offset + phdrs[i].p_filesz;
		if (seg_start > fw->size || seg_end > fw->size)
			return true;
	}

	return false;
}

static int __qcom_mdt_load(struct device *dev, const struct firmware *fw,
			   const char *fw_name, int pas_id, void *mem_region,
			   phys_addr_t mem_phys, size_t mem_size,
			   phys_addr_t *reloc_base, bool pas_init)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct elf32_hdr *ehdr;
	phys_addr_t mem_reloc;
	phys_addr_t min_addr = PHYS_ADDR_MAX;
	ssize_t offset;
	bool relocate = false;
	bool is_split;
	void *ptr;
	int ret = 0;
	int i;

	if (!fw || !mem_region || !mem_phys || !mem_size)
		return -EINVAL;

	if (!mdt_header_valid(fw))
		return -EINVAL;

	is_split = qcom_mdt_bins_are_split(fw, fw_name);
	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(fw->data + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (!mdt_phdr_valid(phdr))
			continue;

		if (phdr->p_flags & QCOM_MDT_RELOCATABLE)
			relocate = true;

		if (phdr->p_paddr < min_addr)
			min_addr = phdr->p_paddr;
	}

	if (relocate) {
		/*
		 * The image is relocatable, so offset each segment based on
		 * the lowest segment address.
		 */
		mem_reloc = min_addr;
	} else {
		/*
		 * Image is not relocatable, so offset each segment based on
		 * the allocated physical chunk of memory.
		 */
		mem_reloc = mem_phys;
	}

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (!mdt_phdr_valid(phdr))
			continue;

		offset = phdr->p_paddr - mem_reloc;
		if (offset < 0 || offset + phdr->p_memsz > mem_size) {
			dev_err(dev, "segment outside memory range\n");
			ret = -EINVAL;
			break;
		}

		if (phdr->p_filesz > phdr->p_memsz) {
			dev_err(dev,
				"refusing to load segment %d with p_filesz > p_memsz\n",
				i);
			ret = -EINVAL;
			break;
		}

		ptr = mem_region + offset;

		if (phdr->p_filesz && !is_split) {
			/* Firmware is large enough to be non-split */
			if (phdr->p_offset + phdr->p_filesz > fw->size) {
				dev_err(dev, "file %s segment %d would be truncated\n",
					fw_name, i);
				ret = -EINVAL;
				break;
			}

			memcpy(ptr, fw->data + phdr->p_offset, phdr->p_filesz);
		} else if (phdr->p_filesz) {
			/* Firmware not large enough, load split-out segments */
			ret = mdt_load_split_segment(ptr, phdrs, i, fw_name, dev);
			if (ret)
				break;
		}

		if (phdr->p_memsz > phdr->p_filesz)
			memset(ptr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
	}

	if (reloc_base)
		*reloc_base = mem_reloc;

	return ret;
}

/**
 * qcom_mdt_load() - load the firmware which header is loaded as fw
 * @dev:	device handle to associate resources with
 * @fw:		firmware object for the mdt file
 * @firmware:	name of the firmware, for construction of segment file names
 * @pas_id:	PAS identifier
 * @mem_region:	allocated memory region to load firmware into
 * @mem_phys:	physical address of allocated memory region
 * @mem_size:	size of the allocated memory region
 * @reloc_base:	adjusted physical address after relocation
 *
 * Returns 0 on success, negative errno otherwise.
 */
int qcom_mdt_load(struct device *dev, const struct firmware *fw,
		  const char *firmware, int pas_id, void *mem_region,
		  phys_addr_t mem_phys, size_t mem_size,
		  phys_addr_t *reloc_base)
{
	int ret;

	ret = __qcom_mdt_pas_init(dev, fw, firmware, pas_id, mem_phys, NULL);
	if (ret)
		return ret;

	return __qcom_mdt_load(dev, fw, firmware, pas_id, mem_region, mem_phys,
			       mem_size, reloc_base, true);
}
EXPORT_SYMBOL_GPL(qcom_mdt_load);

/**
 * qcom_mdt_load_no_init() - load the firmware which header is loaded as fw
 * @dev:	device handle to associate resources with
 * @fw:		firmware object for the mdt file
 * @firmware:	name of the firmware, for construction of segment file names
 * @pas_id:	PAS identifier
 * @mem_region:	allocated memory region to load firmware into
 * @mem_phys:	physical address of allocated memory region
 * @mem_size:	size of the allocated memory region
 * @reloc_base:	adjusted physical address after relocation
 *
 * Returns 0 on success, negative errno otherwise.
 */
int qcom_mdt_load_no_init(struct device *dev, const struct firmware *fw,
			  const char *firmware, int pas_id,
			  void *mem_region, phys_addr_t mem_phys,
			  size_t mem_size, phys_addr_t *reloc_base)
{
	return __qcom_mdt_load(dev, fw, firmware, pas_id, mem_region, mem_phys,
			       mem_size, reloc_base, false);
}
EXPORT_SYMBOL_GPL(qcom_mdt_load_no_init);

/**
 * qcom_mdt_pas_load - Loads and authenticates the metadata of the firmware
 * (typically contained in the .mdt file), followed by loading the actual
 * firmware segments (e.g., .bXX files). Authentication of the segments done
 * by a separate call.
 *
 * The PAS context must be initialized using qcom_scm_pas_context_init()
 * prior to invoking this function.
 *
 * @ctx:        Pointer to the PAS (Peripheral Authentication Service) context
 * @fw:         Firmware object representing the .mdt file
 * @firmware:   Name of the firmware used to construct segment file names
 * @mem_region: Memory region allocated for loading the firmware
 * @reloc_base: Physical address adjusted after relocation
 *
 * Return: 0 on success or a negative error code on failure.
 */
int qcom_mdt_pas_load(struct qcom_scm_pas_context *ctx, const struct firmware *fw,
		      const char *firmware, void *mem_region, phys_addr_t *reloc_base)
{
	int ret;

	ret = __qcom_mdt_pas_init(ctx->dev, fw, firmware, ctx->pas_id, ctx->mem_phys,
				ctx);
	if (ret)
		return ret;

	return __qcom_mdt_load(ctx->dev, fw, firmware, ctx->pas_id,  mem_region,
			       ctx->mem_phys, ctx->mem_size, reloc_base, false);
}
EXPORT_SYMBOL_GPL(qcom_mdt_pas_load);

static void __qcom_mdt_unmap_devmem_rscs(struct qcom_pas_rsc_table_info *info,
					 struct iommu_domain *domain)
{
	struct qcom_pas_devmem_rsc *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &info->devmem_list, node) {
		iommu_unmap(domain, entry->devmem->da, entry->devmem->len);
		list_del(&entry->node);
		kfree(entry);
	}
}

void qcom_mdt_pas_unmap_devmem_rscs(struct qcom_scm_pas_context *ctx, struct iommu_domain *domain)
{
	struct qcom_pas_rsc_table_info *info;

	if (!ctx || !domain)
		return;

	if (!ctx->has_iommu)
		return;

	hash_for_each_possible(qcom_pas_rsc_table_map, info, hnode, ctx->pas_id) {
		if (info->pas_id == ctx->pas_id)
			__qcom_mdt_unmap_devmem_rscs(info, domain);

		hash_del(&info->hnode);
		kfree(info->rsc_table);
	}
}
EXPORT_SYMBOL_GPL(qcom_mdt_pas_unmap_devmem_rscs);

static int __qcom_mdt_map_devmem_rscs(struct device *dev, void *ptr, int avail,
				      struct iommu_domain *domain,
				      struct qcom_pas_rsc_table_info *info)
{
	struct qcom_pas_devmem_rsc *devmem_info;
	struct fw_rsc_devmem *rsc = ptr;
	int ret;

	if (sizeof(*rsc) > avail) {
		dev_err(dev, "devmem rsc is truncated\n");
		return -EINVAL;
	}

	if (rsc->reserved) {
		dev_err(dev, "devmem rsc has non zero reserved bytes\n");
		return -EINVAL;
	}

	devmem_info = kzalloc(sizeof(*devmem_info), GFP_KERNEL);
	if (!devmem_info)
		return -ENOMEM;

	ret = iommu_map(domain, rsc->da, rsc->pa, rsc->len, rsc->flags, GFP_KERNEL);
	if (ret) {
		dev_err(dev, "failed to map devmem: %d\n", ret);
		kfree(devmem_info);
		return ret;
	}

	devmem_info->devmem = rsc;
	list_add_tail(&devmem_info->node, &info->devmem_list);

	dev_dbg(dev, "mapped devmem pa 0x%x, da 0x%x, len 0x%x\n",
		rsc->pa, rsc->da, rsc->len);

	return ret;
}

/**
 * qcom_mdt_pas_map_devmem_rscs() - IOMMU map device memory resources for
 *				     a given Peripheral
 *
 * This routine should be called when it is known that the SoC is running
 * with Linux as hypervisor at EL2 where it is in control of the IOMMU map
 * of the resources for the remote processors.
 *
 * @ctx:	    pas context data structure
 * @domain:	    IOMMU domain
 * @input_rt:	    input resource table buffer when resource table is part of firmware
 *		    binary, if not, pass NULL
 * @input_rt_size:  input resource table size, if input_rt is NULL, then pass zero.
 *
 * Returns 0 on success, negative errno otherwise.
 *
 */
int qcom_mdt_pas_map_devmem_rscs(struct qcom_scm_pas_context *ctx, struct iommu_domain *domain,
				 void *input_rt, size_t input_rt_size)
{
	size_t output_rt_size = MAX_RSCTABLE_SIZE;
	struct resource_table *rsc_table;
	struct qcom_pas_rsc_table_info *info;
	void *output_rt;
	int ret;
	int i;

	if (!ctx || !domain)
		return -EINVAL;

	if (!ctx->has_iommu)
		return 0;

	ret = qcom_scm_pas_get_rsc_table(ctx, input_rt, input_rt_size, &output_rt,
					 &output_rt_size);
	if (ret) {
		dev_err(ctx->dev, "error %d getting resource_table\n", ret);
		return ret;
	}

	rsc_table = output_rt;
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto free_output_rt;
	}

	info->pas_id = ctx->pas_id;
	info->rsc_table = output_rt;
	INIT_LIST_HEAD(&info->devmem_list);
	for (i = 0; i < rsc_table->num; i++) {
		int offset = rsc_table->offset[i];
		struct fw_rsc_hdr *hdr = (void *)rsc_table + offset;
		int avail = output_rt_size - offset - sizeof(*hdr);
		void *ptr = (void *)hdr + sizeof(*hdr);

		if (avail < 0) {
			dev_err(ctx->dev, "rsc table is truncated\n");
			ret = -EINVAL;
			goto undo_mapping;
		}

		if (hdr->type == RSC_DEVMEM) {
			ret = __qcom_mdt_map_devmem_rscs(ctx->dev, ptr, avail, domain, info);
			if (ret)
				goto undo_mapping;
		}
	}

	hash_add(qcom_pas_rsc_table_map, &info->hnode, ctx->pas_id);

	return 0;

undo_mapping:
	__qcom_mdt_unmap_devmem_rscs(info, domain);

free_output_rt:
	kfree(output_rt);

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_mdt_pas_map_devmem_rscs);

MODULE_DESCRIPTION("Firmware parser for Qualcomm MDT format");
MODULE_LICENSE("GPL v2");
