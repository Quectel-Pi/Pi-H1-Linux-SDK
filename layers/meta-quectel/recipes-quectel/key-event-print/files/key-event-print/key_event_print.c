#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <libevdev/libevdev.h>
#include <time.h>
#include <sys/epoll.h>
#include <string.h>
#include <errno.h>

#define KEY1_CODE (114)
#define KEY2_CODE (115)
#define KEY_POWER_CODE (116)
#define HOLD_SECONDS (3)

#define NUM_DEVICES 3
const char *DEVICE_PATHS[NUM_DEVICES] = {
    "/dev/input/event1",
    "/dev/input/event2",
    "/dev/input/event3"
};

int main(void) 
{
    struct libevdev *devs[NUM_DEVICES] = {NULL};
	struct epoll_event ev, events[NUM_DEVICES];
	time_t press_time = 0;
    int fds[NUM_DEVICES];
    int epfd;
	int key_power_down = 0;
    int key1_down = 0;
    int key2_down = 0;


    for (int i = 0; i < NUM_DEVICES; ++i) 
	{
        fds[i] = open(DEVICE_PATHS[i], O_RDONLY | O_NONBLOCK);
        if (fds[i] < 0) {
            perror(DEVICE_PATHS[i]);
            return 1;
        }
        if (libevdev_new_from_fd(fds[i], &devs[i]) < 0) {
            perror("libevdev_new_from_fd");
            return 1;
        }
        printf("Listening on %s (%s)\n", DEVICE_PATHS[i], libevdev_get_name(devs[i]));
    }

    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1 failed");
        return 1;
    }
    
    for (int i = 0; i < NUM_DEVICES; ++i) {
        ev.events = EPOLLIN;
        ev.data.u32 = i; 
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fds[i], &ev) < 0) {
            perror("epoll_ctl");
            return 1;
        }
    }

    while (1) 
	{
        int nfds = epoll_wait(epfd, events, NUM_DEVICES, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int n = 0; n < nfds; ++n) 
		{
            int idx = events[n].data.u32;
            struct input_event iev;
            while (libevdev_next_event(devs[idx], LIBEVDEV_READ_FLAG_NORMAL, &iev) == 0) 
			{
				 //power key check
				 if (iev.type == EV_KEY && iev.code == KEY_POWER_CODE) 
				 {
					if (iev.value == 1) 
					{  // key down
						press_time = time(NULL);
						key_power_down = 1;
					} 
					else if (iev.value == 0 && key_power_down) {  // key up
						time_t now = time(NULL);
						int held = (int)(now - press_time);
						key_power_down = 0;

						if (held >= HOLD_SECONDS) {
							printf("Power key held %d seconds. Shutting down...\n", held);
							(void)system("sync");
							(void)system("shutdown -h now");
							goto cleanup;
						} 
						else 
						{
							printf("Power key pressed.\n");
						}
					}
				}
				
				//another two keys check
				else if(iev.type == EV_KEY && (iev.code == KEY1_CODE || iev.code == KEY2_CODE))
				{
					if (iev.code == KEY1_CODE) {
						if (iev.value == 1) { // key1 down
							key1_down = 1;
						} else if (iev.value == 0 && key1_down) { // key1 up
							key1_down = 0;
							printf("key 1 pressed.\n");
						}
					} else if (iev.code == KEY2_CODE) {
						if (iev.value == 1) { // key2 down
							key2_down = 1;
						} else if (iev.value == 0 && key2_down) { // key2 up
							key2_down = 0;
							printf("key 2 pressed.\n");
						}
					}
				}
            }
        }
    }

cleanup:
    for (int i = 0; i < NUM_DEVICES; ++i) {
        if (devs[i]) {
            libevdev_free(devs[i]);
        }
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }
    close(epfd);
    return 0;
}
