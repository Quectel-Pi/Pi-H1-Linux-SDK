#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <libevdev/libevdev.h>
#include <time.h>
#include <sys/epoll.h>
#include <string.h>
#include <errno.h>

#define DEVICE_PATH "/dev/input/event2"
#define KEY_POWER 116
#define HOLD_SECONDS 3

int main(void) {
    struct libevdev *dev = NULL;
    int fd, epfd;
    struct epoll_event ev, events[1];
    time_t press_time = 0;
    int key_down = 0;

    fd = open(DEVICE_PATH, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open input device");
        return 1;
    }

    if (libevdev_new_from_fd(fd, &dev) < 0) {
        perror("Failed to init libevdev");
        return 1;
    }

    printf("Listening on %s (%s)\n", DEVICE_PATH, libevdev_get_name(dev));

    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1 failed");
        return 1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl failed");
        return 1;
    }

    while (1) {
        int nfds = epoll_wait(epfd, events, 1, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        struct input_event ev;
        while (libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev) == 0) {
            if (ev.type == EV_KEY && ev.code == KEY_POWER) {
                if (ev.value == 1) {  // key down
                    press_time = time(NULL);
                    key_down = 1;
                } else if (ev.value == 0 && key_down) {  // key up
                    time_t now = time(NULL);
                    int held = (int)(now - press_time);
                    key_down = 0;

                    if (held >= HOLD_SECONDS) {
                        printf("Power key held %d seconds. Shutting down...\n", held);
                        (void)system("sync");
                        (void)system("shutdown -h now");
                        goto cleanup;
                    } else {
                        printf("Power key held %d seconds. Ignored.\n", held);
                    }
                }
            }
        }
    }

cleanup:
    libevdev_free(dev);
    close(fd);
    close(epfd);
    return 0;
}
