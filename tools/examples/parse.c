#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define FRAME_LEN 21

int main(void)
{
    uint8_t raw[FRAME_LEN];
    int count = 0;

    /* Read 21 hex bytes from stdin */
    while (count < FRAME_LEN) {
        unsigned int x;
        if (scanf("%x", &x) != 1) {
            fprintf(stderr, "ERROR: expected %d hex bytes\n", FRAME_LEN);
            return 1;
        }
        raw[count++] = (uint8_t)x;
    }

    /* Byte 0 = padding */

    uint8_t msg_type = raw[1];
    uint16_t source  = (raw[2] << 8) | raw[3];

    uint8_t fblock = raw[4];
    uint8_t inst   = raw[5];

    uint16_t fktid = (raw[6] << 4) | (raw[7] >> 4);
    uint8_t  optype = raw[7] & 0x0F;

    uint8_t telid  = raw[8] >> 4;
    uint8_t tellen = raw[8] & 0x0F;

    printf("MsgType   : 0x%02X\n", msg_type);
    printf("Source    : 0x%04X\n", source);
    printf("FBlockID  : 0x%02X\n", fblock);
    printf("InstID    : 0x%02X\n", inst);
    printf("FktID     : 0x%03X\n", fktid);
    printf("OpType    : %u\n", optype);
    printf("TelID     : %u\n", telid);
    printf("TelLen    : %u\n", tellen);

    printf("Data      : ");
    for (int i = 0; i < tellen; i++)
        printf("%02X ", raw[9 + i]);
    printf("\n");

    return 0;
}
