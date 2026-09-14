
#include "../atcid_util.h"

ATRESPONSE_t QL_AT_ATI_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QGMR_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_EGMR_Handle(char* cmdline, ATOP_t opType, char* response);

ATRESPONSE_t QL_AT_QWSETMAC_Handle(char* cmdline, ATOP_t opType, char* response);

ATRESPONSE_t QL_AT_QADC_Handle(char* cmdline, ATOP_t opType, char* response);

ATRESPONSE_t QL_AT_QSCLK_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QTEST_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QFTCMD_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_CFUN_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QPCIE_Handle(char* cmdline, ATOP_t at_op, char* response);
ATRESPONSE_t QL_AT_IPR_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QUIMSLOT_Handle(char *cmdline, ATOP_t opType, char *response);
ATRESPONSE_t QL_AT_GMI_Handle(char *cmdline, ATOP_t opType, char *response);
ATRESPONSE_t QL_AT_GMM_Handle(char *cmdline, ATOP_t opType, char *response);
ATRESPONSE_t QL_AT_CSUB_Handle(char *cmdline, ATOP_t opType, char *response);
ATRESPONSE_t QL_AT_QHVN_Handle(char *cmdline, ATOP_t opType, char *response);
ATRESPONSE_t QL_AT_QSVN_Handle(char *cmdline, ATOP_t opType, char *response);
ATRESPONSE_t QL_AT_QCFG_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QBASELINE_Handle(char* cmdline, ATOP_t opType, char* response);

ATRESPONSE_t QL_AT_QFCT_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QNVW_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QNVR_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QAPSUB_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_HEADSET_START_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_CAMERA0_START_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_CAMERA1_START_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_CAMERA2_START_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_CAMERA_STOP_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_KEYGET_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_CARDGET_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_LEDREDON_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_LEDREDOFF_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_LEDGREENON_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_LEDGREENOFF_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_LEDBLUEON_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_LEDBLUEOFF_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_SSDTEST_Handle   (char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_40PINLEDON_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_40PINLEDOFF_Handle(char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_FANON_Handle      (char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_FANOFF_Handle     (char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QCFG_Handle (char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QUSBMODE_Handle (char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QREDDAVN_Handle (char* cmdline, ATOP_t opType, char* response);
ATRESPONSE_t QL_AT_QMAC_Handle (char* cmdline, ATOP_t opType, char* response);