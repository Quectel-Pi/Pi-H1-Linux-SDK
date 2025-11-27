// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

void qcom_scm_set_download_mode(bool);
static int qcom_scm2_init(void)
{
    qcom_scm_set_download_mode(true);
	return 0;
}

static void __exit qcom_scm2_fini(void)
{
    qcom_scm_set_download_mode(false);
}

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. SCM2 driver, enable and disable download_mode");
MODULE_LICENSE("GPL v2");
module_init(qcom_scm2_init);
module_exit(qcom_scm2_fini);
MODULE_AUTHOR("arthur.yao");
