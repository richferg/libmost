#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "most.h"

#define FRAME_LEN 21

int main(int argc, char **argv)
{
    const char *dev = "0";  // default /dev/most0
    if (argc >= 2)
        dev = argv[1];

    struct most_handle *h = most_open(dev);
    if (!h) {
        perror("most_open");
        return 1;
    }

    uint8_t frame[FRAME_LEN];
    int count = 0;

    printf("Enter 21 hex bytes:\n");

    while (count < FRAME_LEN) {
        unsigned int x;
        int r = scanf("%x", &x);
        if (r != 1) {
            fprintf(stderr, "ERROR: expected hex byte at position %d\n", count);
            most_close(h);
            return 1;
        }
        frame[count++] = (uint8_t)x;
    }

    printf("Sending frame to /dev/most%s...\n", dev);

    if (most_write(h, frame) != 0) {
        perror("most_write");
        most_close(h);
        return 1;
    }

    printf("Done.\n");
    most_close(h);
    return 0;
}
