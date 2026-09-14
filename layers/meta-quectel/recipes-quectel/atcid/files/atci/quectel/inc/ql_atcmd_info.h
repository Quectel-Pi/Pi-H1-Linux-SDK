#include "../atcid_util.h"
#include "../atcid_cust_cmd_process.h"

#include "ql_at_factory.h"
#include "ql_temp_cmd.h"

#define MAX_AT_COMMAND_LEN 32

/*
    AT_SET_OP           AT+XXX=*
    AT_READ_OP          AT+XXX?
    AT_TEST_OP          AT+XXX=?
    AT_BASIC_OP         AT*
*/


//To Quectel developer: add new AT here:
static customcmd_type quec_at_cmd_table[] = {
        {"ATI", AT_BASIC_OP , QL_AT_ATI_Handle},
        {"AT+QGMR", AT_ACTION_OP | AT_READ_OP | AT_TEST_OP , QL_AT_QGMR_Handle},
        {"AT+EGMR", AT_SET_OP | AT_READ_OP | AT_TEST_OP , QL_AT_EGMR_Handle},
        {"AT+QWSETMAC", AT_READ_OP | AT_SET_OP | AT_TEST_OP , QL_AT_QWSETMAC_Handle},
        {"AT+QADC", AT_READ_OP | AT_SET_OP | AT_TEST_OP , QL_AT_QADC_Handle},
        {"AT+QSCLK", AT_READ_OP | AT_SET_OP | AT_TEST_OP , QL_AT_QSCLK_Handle},
        {"AT+QTEST", AT_READ_OP | AT_SET_OP | AT_TEST_OP , QL_AT_QTEST_Handle},
        {"AT+QFTCMD", AT_READ_OP | AT_SET_OP | AT_TEST_OP , QL_AT_QFTCMD_Handle},
        {"AT+CFUN", AT_READ_OP | AT_SET_OP | AT_TEST_OP | AT_ACTION_OP  , QL_AT_CFUN_Handle},
        {"AT+QPCIE", AT_SET_OP | AT_TEST_OP, QL_AT_QPCIE_Handle},
        {"AT+IPR", AT_READ_OP | AT_SET_OP | AT_TEST_OP , QL_AT_IPR_Handle},
        {"AT+QUIMSLOT", AT_READ_OP | AT_SET_OP | AT_TEST_OP , QL_AT_QUIMSLOT_Handle},
        {"AT+QTEMP",  AT_ACTION_OP | AT_READ_OP | AT_TEST_OP ,QL_AT_QTEMP_Handle},
        {"AT+GMI",  AT_ACTION_OP | AT_TEST_OP, QL_AT_GMI_Handle},
        {"AT+CGMI", AT_ACTION_OP | AT_TEST_OP, QL_AT_GMI_Handle},
        {"AT+GMM",  AT_ACTION_OP | AT_TEST_OP, QL_AT_GMM_Handle},
        {"AT+CGMM", AT_ACTION_OP | AT_TEST_OP, QL_AT_GMM_Handle},
        {"AT+GMR",  AT_ACTION_OP | AT_TEST_OP, QL_AT_QGMR_Handle},
        {"AT+CGMR", AT_ACTION_OP | AT_TEST_OP, QL_AT_QGMR_Handle},
        {"AT+CSUB", AT_ACTION_OP | AT_TEST_OP, QL_AT_CSUB_Handle},
        {"AT+QHVN", AT_ACTION_OP | AT_TEST_OP, QL_AT_QHVN_Handle},
        {"AT+QSVN", AT_ACTION_OP | AT_TEST_OP, QL_AT_QSVN_Handle},
        {"AT+QBASELINE",AT_ACTION_OP | AT_TEST_OP , QL_AT_QBASELINE_Handle},
        {"AT+QFCT",AT_SET_OP | AT_TEST_OP , QL_AT_QFCT_Handle},
        {"AT+QNVW",AT_SET_OP | AT_TEST_OP , QL_AT_QNVW_Handle},
        {"AT+QNVR",AT_SET_OP | AT_TEST_OP , QL_AT_QNVR_Handle},
        {"AT+QAPSUB", AT_ACTION_OP | AT_READ_OP | AT_TEST_OP  , QL_AT_QAPSUB_Handle},
        {"AT+HEADSET_START", AT_ACTION_OP | AT_TEST_OP, QL_AT_HEADSET_START_Handle},
        {"AT+CAMERA0_START", AT_ACTION_OP | AT_TEST_OP, QL_AT_CAMERA0_START_Handle},
        {"AT+CAMERA1_START", AT_ACTION_OP | AT_TEST_OP, QL_AT_CAMERA1_START_Handle},
        {"AT+CAMERA2_START", AT_ACTION_OP | AT_TEST_OP, QL_AT_CAMERA2_START_Handle},
        {"AT+CAMERA_STOP"  , AT_ACTION_OP | AT_TEST_OP, QL_AT_CAMERA_STOP_Handle},
        {"AT+KEYGET"       , AT_ACTION_OP | AT_TEST_OP, QL_AT_KEYGET_Handle},
        {"AT+CARDGET"      , AT_ACTION_OP | AT_TEST_OP, QL_AT_CARDGET_Handle},
        {"AT+LEDREDON"     , AT_ACTION_OP | AT_TEST_OP, QL_AT_LEDREDON_Handle},
        {"AT+LEDREDOFF"    , AT_ACTION_OP | AT_TEST_OP, QL_AT_LEDREDOFF_Handle},
        {"AT+LEDGREENON"   , AT_ACTION_OP | AT_TEST_OP, QL_AT_LEDGREENON_Handle},
        {"AT+LEDGREENOFF"  , AT_ACTION_OP | AT_TEST_OP, QL_AT_LEDGREENOFF_Handle},
        {"AT+LEDBLUEON"    , AT_ACTION_OP | AT_TEST_OP, QL_AT_LEDBLUEON_Handle},
        {"AT+LEDBLUEOFF"   , AT_ACTION_OP | AT_TEST_OP, QL_AT_LEDBLUEOFF_Handle},
        {"AT+SSDTEST"      , AT_ACTION_OP | AT_TEST_OP, QL_AT_SSDTEST_Handle},
        {"AT+40PINLEDON"   , AT_ACTION_OP | AT_TEST_OP, QL_AT_40PINLEDON_Handle},
        {"AT+40PINLEDOFF"  , AT_ACTION_OP | AT_TEST_OP, QL_AT_40PINLEDOFF_Handle},
        {"AT+FANON"        , AT_ACTION_OP | AT_TEST_OP, QL_AT_FANON_Handle},
        {"AT+FANOFF"       , AT_ACTION_OP | AT_TEST_OP, QL_AT_FANOFF_Handle},
        {"AT+QCFG"         , AT_READ_OP | AT_SET_OP | AT_TEST_OP, QL_AT_QCFG_Handle},
        {"AT+QUSBMODE"      , AT_ACTION_OP | AT_TEST_OP, QL_AT_QUSBMODE_Handle},
        {"AT+QREDDAVN"      , AT_ACTION_OP | AT_READ_OP, QL_AT_QREDDAVN_Handle},
        {"AT+QMAC"      , AT_READ_OP | AT_SET_OP | AT_TEST_OP , QL_AT_QMAC_Handle},
    };


#define ql_at_system(...)  system(__VA_ARGS__)
