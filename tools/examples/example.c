#include <stdio.h>
#include "most.h"

int main(void)
{
    struct most_handle *h = most_open("0");
    if (!h) {
        perror("most_open");
        return 1;
    }

    uint8_t frame[MOST_FRAME_LEN] = {0};

    /* Send something */
    frame[0] = 0x01; // priority etc...
    if (most_write(h, frame) != 0) {
        perror("most_write");
    }

    /* Poll for response */
    if (most_poll(h, 1000) == 1) {
        if (most_read(h, frame) == 0) {
            printf("Got RCMB: ");
            for (int i = 0; i < MOST_FRAME_LEN; i++)
                printf("%02X ", frame[i]);
            printf("\n");
        }
    }

    most_close(h);
    return 0;
}
