#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

struct i2c_client {
    int bus;
    int addr;
    int fd;
};

static int is_sgm7220(const struct i2c_client *client) {
    return client->bus == 1 && client->addr == 0x47;
}

static int test_i2c_transfer(struct i2c_client *client, struct i2c_msg *msgs, int num)
{
    struct i2c_rdwr_ioctl_data msgset;
    int ret;

    msgset.msgs = msgs;
    msgset.nmsgs = num;

    ret = ioctl(client->fd, I2C_RDWR, &msgset);
    if (ret < 0) {
        printf("Failed to transfer data to the i2c bus, ret: %d, errno: %d (%s)\n", ret, errno, strerror(errno));
    }
    return ret;
}
static int test_i2c_write(struct i2c_client *client, uint8_t *buf, int data_len)
{
    int i;
    struct i2c_msg msg_set[1] = {
        {
            .addr = client->addr,
            .flags = 0,
            .len = 2,
            .buf = (uint8_t *)buf
        }
    };

    if (is_sgm7220(client)) {
        msg_set[0].flags |= I2C_M_IGNORE_NAK;
    }

    return test_i2c_transfer(client, msg_set, 1);
}

static int test_i2c_read(struct i2c_client *client, uint8_t *buf, int addr_len, int data_len)
{
    int i;
    struct i2c_msg msg_set[2] = {
        {
            .addr = client->addr,
            .flags = 0,
            .len = addr_len,
            .buf = buf
        },
        {
            .addr = client->addr,
            .flags = I2C_M_RD,
            .len = data_len,
            .buf = buf
        }
    };

    if (is_sgm7220(client)) {
        msg_set[0].flags |= I2C_M_REV_DIR_ADDR;
        msg_set[1].flags |= I2C_M_NOSTART;
    }


    return test_i2c_transfer(client, msg_set, 2);
}

int main(int argc, char *argv[]) {
    char filename[20];
    int i;
    struct i2c_client client;

    if (argc < 6) {
        printf("%s bus slave_addr r addr_len addr_array data_len\n", argv[0]);
        printf("%s bus slave_addr w data_len data_array\n", argv[0]);
        return -1;
    }
    client.bus = strtol(argv[1], NULL, 10);
    client.addr = strtol(argv[2], NULL, 16);

    printf("bus=%d, addr=0x%x\n", client.bus, client.addr);
    snprintf(filename, 19, "/dev/i2c-%d", client.bus);
    client.fd = open(filename, O_RDWR);
    if (client.fd < 0) {
        printf("Failed to open the i2c bus, errno: %d (%s)\n", errno, strerror(errno));
        return -1;
    }

    if (ioctl(client.fd, I2C_SLAVE_FORCE, client.addr) < 0) {
        printf("Failed to acquire bus access and/or talk to slave, errno: %d (%s)\n", errno, strerror(errno));
        close(client.fd);
        return -1;
    }

    i = 3;
    while (i < argc) {
        const char *op = argv[i++];
        uint8_t buf[128];
        int j, addr_len, data_len = 0;

        printf("op: %s, ", op);

        if (i >= argc) {
            printf("error i=%d, argc=%d\n", i, argc);
            goto _out;
        }
        data_len = strtol(argv[i++], NULL, 10);
        if (data_len <= 0 || data_len > sizeof(buf)) {
            printf(" error i=%d, argc=%d, data_len=%d\n", i, argc, data_len);
            goto _out;
        }

        printf("%s: %d [", strcmp(op, "r") == 0 ? "addr_len" : "data_len", data_len);
        for (j = 0; j < data_len; j++) {
            if (i >= argc) {
                printf(" error i=%d, argc=%d, j=%d\n", i, argc, j);
                goto _out;
            }
            buf[j] = strtol(argv[i++], NULL, 16);
            printf(" %02x", buf[j]);
        }
        printf(" ]\n");

        if (strcmp(op, "r") == 0) {
            addr_len = data_len;
            if (i >= argc) {
                printf(" error i=%d, argc=%d\n", i, argc);
                goto _out;
            }
            data_len = strtol(argv[i++], NULL, 10);
            printf("to read data len : %d\n", data_len);

            if (test_i2c_read(&client, buf, addr_len, data_len) <= 0) {
                break;
            }
            for (j = 0; j < data_len; j++) {
                printf("  recv %02x\n", buf[j]);
            }
        }
        else if (strcmp(op, "w") == 0) {
            printf("to write data len : %d\n", data_len);
            if (test_i2c_write(&client, buf, data_len) < 0) {
                break;
            }
        }
        else {
            printf("Invalid command %s\n", argv[i]);
            break;
        }
    }
_out:

    close(client.fd);

    return 0;
}
