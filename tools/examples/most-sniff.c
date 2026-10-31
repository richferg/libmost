#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <string.h>

#include "segmented.h"

#include "most.h"

static int stop = 0;

/* ---------- Signal handler ---------- */
static void handle_sigint(int sig)
{
    (void)sig;  // unused
    stop = 1;
}

/* ---------- Timestamp helper ---------- */
static void timestamp_now(char *buf, size_t buflen)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    snprintf(buf, buflen,
             "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
             tm.tm_year + 1900,
             tm.tm_mon + 1,
             tm.tm_mday,
             tm.tm_hour,
             tm.tm_min,
             tm.tm_sec,
             ts.tv_nsec / 1000000);
}

static void handle_complete_cmd(const struct most_pdu *p, void *userdata)
{
    FILE *log = (FILE *)userdata;

    printf("DECODED CMD: len=%d\n", p->payload_len);
    printf("Src=0x%04X MsgType=0x%02X FBlock=0x%02X Inst=0x%02X FktID=0x%03X Op=%X\n",
           p->source, p->msg_type, p->fblock, p->inst, p->fktid, p->optype);

    fprintf(log, "DECODED CMD len=%d: ", p->payload_len);
    fprintf(log, "Src=0x%04X MsgType=0x%02X FBlock=0x%02X Inst=0x%02X FktID=0x%03X Op=%X Payload=",
            p->source, p->msg_type, p->fblock, p->inst, p->fktid, p->optype);
    for (int i = 0; i < p->payload_len; i++)
        fprintf(log, "%02X ", p->payload[i]);
    fprintf(log, "\n");
    fflush(log);
}

/* ---------- Main ---------- */
int main(int argc, char **argv)
{
    const char *dev = "0";               // default /dev/most0
    const char *outfile = "most-sniff.log";

    /* Parse arguments */
    if (argc >= 2)
        dev = argv[1];
    if (argc >= 3)
        outfile = argv[2];

    printf("MOST25 Sniffer: Listening on /dev/most%s\n", dev);
    printf("Logging to: %s\n", outfile);

    /* Open log file */
    FILE *log = fopen(outfile, "a");
    if (!log) {
        perror("fopen");
        return 1;
    }
    /* Line-buffer the log so entries hit disk promptly */
    setvbuf(log, NULL, _IOLBF, 0);

    struct most_handle *h = most_open(dev);
    if (!h) {
        perror("most_open");
        fclose(log);
        return 1;
    }

    signal(SIGINT, handle_sigint);

    uint8_t frame[MOST_FRAME_LEN];

    while (!stop) {

        int p = most_poll(h, 1000);
        if (p < 0) {
            perror("poll");
            break;
        }
        if (p == 0)
            continue;

        if (most_read(h, frame) != 0) {
            perror("most_read");
            break;
        }

        char ts[64];
        timestamp_now(ts, sizeof(ts));

        /* Print raw frame */
        printf("%s  ", ts);
        for (int i = 0; i < MOST_FRAME_LEN; i++)
            printf("%02X ", frame[i]);
        printf("\n");

        fprintf(log, "%s  ", ts);
        for (int i = 0; i < MOST_FRAME_LEN; i++)
            fprintf(log, "%02X ", frame[i]);
        fprintf(log, "\n");
        fflush(log);

        /* Feed the segmented parser */
        segmented_process_frame(frame, handle_complete_cmd, log);
    }

    printf("\nExiting sniffer.\n");

    most_close(h);
    fclose(log);
    return 0;
}
