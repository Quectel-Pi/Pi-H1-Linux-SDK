#define fsg_head_string "Quec-Head-nv1-v1"
#define fsg_tail_string "Quec-Tail-nv1-v1"
#define fsc_head_string "Quec-Head-nv2-v1"
#define fsc_tail_string "Quec-Tail-nv2-v1"

#define DEV_DATA "/var/persist/NV_data"
#define DEV_BACKUP "/var/persist/NV_data_backup"

#define MP_VERSION_LEN 32
#define BP_VERSION_LEN 32

typedef struct {
    char magic_head[16];
    char enable_dump[16];
    char mp_version[MP_VERSION_LEN];
    char bp_version[BP_VERSION_LEN];
    char sn[128];
    char wifi_mac[16];
    char bt_mac[16];
    char qcsn[128];           
    char nv_data[128];
    char magic_tail[16];
    char ati_info[32];
    char key_data[64];
} Quec_wifi_fsg_data; 



int modem_backup(void);
int modem_sync(void);
int fct_data_init(void);

void start_bluetooth_power(void);
void start_bluetooth(void);
void stop_bluetooth(void);
void start_wifi(void);
void stop_wifi(void);
void restart_eth(void);

int validateInput(const char* input);
int extract_data(char* read_br,int start_num);