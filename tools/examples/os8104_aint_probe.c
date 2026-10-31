#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define BIT(n) (1u << (n))

#define OS8104_IOC_MAGIC       'm'
#define OS8104_IOC_REG_READ    _IOWR(OS8104_IOC_MAGIC, 0x09, struct os8104_reg_io)
#define OS8104_IOC_REG_WRITE   _IOW(OS8104_IOC_MAGIC, 0x0a, struct os8104_reg_io)

#define OS8104_REG_BNAH    0x008a
#define OS8104_REG_BNAL    0x008b
#define OS8104_REG_BPCTC   0x00e2
#define OS8104_REG_BPCTS   0x00e3
#define OS8104_REG_BPSTX   0x00ea
#define OS8104_REG_BPLDT   0x00ec
#define OS8104_REG_BPPI    0x00f2
#define OS8104_REG_MAXP    0x01c0

#define OS8104_BPCTC_RAF  BIT(4)
#define OS8104_BPCTC_RAC  BIT(3)
#define OS8104_BPCTC_RATX BIT(1)
#define OS8104_BPCTC_RARX BIT(0)

#define OS8104_BPCTS_AF   BIT(4)
#define OS8104_BPCTS_AC   BIT(3)
#define OS8104_BPCTS_ATX  BIT(1)
#define OS8104_BPCTS_ARX  BIT(0)

#define OS8104_BPSTX_ASTX BIT(7)
#define MAX_PAYLOAD 48

struct os8104_reg_io {
    uint16_t reg;
    uint16_t len;
    uint8_t data[62];
};

struct config {
    const char *devnode;
    unsigned int timeout_ms;
    unsigned int poll_ms;
    uint16_t target_addr;
    int target_addr_set;
    uint8_t priority;
    uint8_t payload[MAX_PAYLOAD];
    size_t payload_len;
};

static const struct option long_opts[] = {
    {"device", required_argument, NULL, 'D'},
    {"target", required_argument, NULL, 'a'},
    {"timeout-ms", required_argument, NULL, 't'},
    {"poll-ms", required_argument, NULL, 'p'},
    {"payload", required_argument, NULL, 'P'},
    {"priority", required_argument, NULL, 'r'},
    {NULL, 0, NULL, 0},
};

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [-D /dev/mostN] [-a target_hex] [-t timeout_ms]\n"
            "          [-p poll_ms] [-P hexbytes] [-r priority]\n"
            "\n"
            "Triggers one OS8104 async packet transaction through raw register ioctls\n"
            "and polls bPCTS so GPIO monitoring can be done in parallel with gpiomon.\n"
            "\n"
            "Examples:\n"
            "  %s -D /dev/most0\n"
            "  %s -D /dev/most0 -a 0x0110 -P deadbeef\n",
            argv0, argv0, argv0);
}

static uint64_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void msleep_exact(unsigned int ms)
{
    struct timespec req;

    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&req, &req) && errno == EINTR)
        ;
}

static int parse_hex_u16(const char *s, uint16_t *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 0);

    if (!s[0] || *end || v > 0xffff)
        return -EINVAL;
    *out = (uint16_t)v;
    return 0;
}

static int parse_u32(const char *s, unsigned int *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 0);

    if (!s[0] || *end || v > 0xffffffffUL)
        return -EINVAL;
    *out = (unsigned int)v;
    return 0;
}

static int parse_payload(struct config *cfg, const char *arg)
{
    size_t len = strlen(arg);
    size_t i;

    if (len == 0 || (len & 1))
        return -EINVAL;
    len /= 2;
    if (len > MAX_PAYLOAD)
        return -E2BIG;

    for (i = 0; i < len; i++) {
        char byte_str[3] = { arg[i * 2], arg[i * 2 + 1], '\0' };
        char *end;
        unsigned long v = strtoul(byte_str, &end, 16);

        if (*end || v > 0xff)
            return -EINVAL;
        cfg->payload[i] = (uint8_t)v;
    }
    cfg->payload_len = len;
    return 0;
}

static int os8104_reg_read(int fd, uint16_t reg, uint8_t *data, size_t len)
{
    struct os8104_reg_io reg_io;

    if (len > sizeof(reg_io.data))
        return -EINVAL;
    memset(&reg_io, 0, sizeof(reg_io));
    reg_io.reg = reg;
    reg_io.len = (uint16_t)len;
    if (ioctl(fd, OS8104_IOC_REG_READ, &reg_io) < 0)
        return -errno;
    memcpy(data, reg_io.data, len);
    return 0;
}

static int os8104_reg_write(int fd, uint16_t reg, const uint8_t *data, size_t len)
{
    struct os8104_reg_io reg_io;

    if (len > sizeof(reg_io.data))
        return -EINVAL;
    memset(&reg_io, 0, sizeof(reg_io));
    reg_io.reg = reg;
    reg_io.len = (uint16_t)len;
    memcpy(reg_io.data, data, len);
    if (ioctl(fd, OS8104_IOC_REG_WRITE, &reg_io) < 0)
        return -errno;
    return 0;
}

static void print_bpcts(uint8_t v)
{
    printf("bPCTS=0x%02x%s%s%s%s\n",
           v,
           (v & OS8104_BPCTS_AF) ? " AF" : "",
           (v & OS8104_BPCTS_AC) ? " AC" : "",
           (v & OS8104_BPCTS_ATX) ? " ATX" : "",
           (v & OS8104_BPCTS_ARX) ? " ARX" : "");
}

int main(int argc, char **argv)
{
    struct config cfg = {
        .devnode = "/dev/most0",
        .timeout_ms = 1500,
        .poll_ms = 5,
        .priority = 0x01,
        .payload = { 0xde, 0xad, 0xbe, 0xef },
        .payload_len = 4,
    };
    int fd = -1;
    int opt;
    int ret;
    uint8_t node_addr[2];
    uint16_t target_addr;
    uint8_t bpcts = 0;
    uint8_t pctc = 0;
    uint8_t pldt;
    uint8_t axp[2 + MAX_PAYLOAD] = {0};
    uint64_t start;

    while ((opt = getopt_long(argc, argv, "D:a:t:p:P:r:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'D':
            cfg.devnode = optarg;
            break;
        case 'a':
            if (parse_hex_u16(optarg, &cfg.target_addr)) {
                fprintf(stderr, "invalid target address: %s\n", optarg);
                return 2;
            }
            cfg.target_addr_set = 1;
            break;
        case 't':
            if (parse_u32(optarg, &cfg.timeout_ms)) {
                fprintf(stderr, "invalid timeout: %s\n", optarg);
                return 2;
            }
            break;
        case 'p':
            if (parse_u32(optarg, &cfg.poll_ms) || cfg.poll_ms == 0) {
                fprintf(stderr, "invalid poll interval: %s\n", optarg);
                return 2;
            }
            break;
        case 'P':
            if (parse_payload(&cfg, optarg)) {
                fprintf(stderr, "invalid payload hex: %s\n", optarg);
                return 2;
            }
            break;
        case 'r': {
            unsigned int prio;
            if (parse_u32(optarg, &prio) || prio < 1 || prio > 7) {
                fprintf(stderr, "invalid priority: %s\n", optarg);
                return 2;
            }
            cfg.priority = (uint8_t)prio;
            break;
        }
        case 'h':
        default:
            usage(argv[0]);
            return (opt == 'h') ? 0 : 2;
        }
    }

    fd = open(cfg.devnode, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror(cfg.devnode);
        return 1;
    }

    ret = os8104_reg_read(fd, OS8104_REG_BNAH, node_addr, sizeof(node_addr));
    if (ret) {
        fprintf(stderr, "failed to read node address: %s\n", strerror(-ret));
        goto out;
    }

    target_addr = cfg.target_addr_set ? cfg.target_addr :
                  ((uint16_t)node_addr[0] << 8) | node_addr[1];
    printf("Using %s target=0x%04x payload_len=%zu priority=%u\n",
           cfg.devnode, target_addr, cfg.payload_len, cfg.priority);

    ret = os8104_reg_read(fd, OS8104_REG_BPCTS, &bpcts, 1);
    if (ret) {
        fprintf(stderr, "failed to read bPCTS: %s\n", strerror(-ret));
        goto out;
    }
    printf("Initial ");
    print_bpcts(bpcts);

    pctc = 0;
    if (bpcts & OS8104_BPCTS_AF)
        pctc |= OS8104_BPCTC_RAF;
    if (bpcts & OS8104_BPCTS_AC)
        pctc |= OS8104_BPCTC_RAC;
    if (bpcts & OS8104_BPCTS_ATX)
        pctc |= OS8104_BPCTC_RATX;
    if (bpcts & OS8104_BPCTS_ARX)
        pctc |= OS8104_BPCTC_RARX;
    if (pctc) {
        ret = os8104_reg_write(fd, OS8104_REG_BPCTC, &pctc, 1);
        if (ret) {
            fprintf(stderr, "failed to clear stale bPCTS flags: %s\n", strerror(-ret));
            goto out;
        }
        msleep_exact(20);
    }

    pldt = (uint8_t)((cfg.payload_len + 3) / 4);
    ret = os8104_reg_write(fd, OS8104_REG_BPPI, &cfg.priority, 1);
    if (ret) {
        fprintf(stderr, "failed to write bPPI: %s\n", strerror(-ret));
        goto out;
    }
    ret = os8104_reg_write(fd, OS8104_REG_BPLDT, &pldt, 1);
    if (ret) {
        fprintf(stderr, "failed to write bPLDT: %s\n", strerror(-ret));
        goto out;
    }

    axp[0] = (uint8_t)(target_addr >> 8);
    axp[1] = (uint8_t)(target_addr & 0xff);
    memcpy(&axp[2], cfg.payload, cfg.payload_len);
    ret = os8104_reg_write(fd, OS8104_REG_MAXP, axp, 2 + cfg.payload_len);
    if (ret) {
        fprintf(stderr, "failed to write mAXP: %s\n", strerror(-ret));
        goto out;
    }

    pctc = OS8104_BPSTX_ASTX;
    ret = os8104_reg_write(fd, OS8104_REG_BPSTX, &pctc, 1);
    if (ret) {
        fprintf(stderr, "failed to set ASTX: %s\n", strerror(-ret));
        goto out;
    }

    printf("Started async TX; polling bPCTS for %u ms\n", cfg.timeout_ms);
    start = now_ms();
    do {
        uint8_t cur = 0;

        ret = os8104_reg_read(fd, OS8104_REG_BPCTS, &cur, 1);
        if (ret) {
            fprintf(stderr, "failed to read bPCTS: %s\n", strerror(-ret));
            goto out;
        }
        if (cur != bpcts) {
            printf("[%llums] ", (unsigned long long)(now_ms() - start));
            print_bpcts(cur);
            bpcts = cur;
        }
        if (bpcts & (OS8104_BPCTS_ATX | OS8104_BPCTS_ARX | OS8104_BPCTS_AF | OS8104_BPCTS_AC))
            break;
        msleep_exact(cfg.poll_ms);
    } while ((now_ms() - start) < cfg.timeout_ms);

    if (bpcts & (OS8104_BPCTS_ATX | OS8104_BPCTS_ARX | OS8104_BPCTS_AF | OS8104_BPCTS_AC)) {
        pctc = 0;
        if (bpcts & OS8104_BPCTS_AF)
            pctc |= OS8104_BPCTC_RAF;
        if (bpcts & OS8104_BPCTS_AC)
            pctc |= OS8104_BPCTC_RAC;
        if (bpcts & OS8104_BPCTS_ATX)
            pctc |= OS8104_BPCTC_RATX;
        if (bpcts & OS8104_BPCTS_ARX)
            pctc |= OS8104_BPCTC_RARX;
        if (pctc) {
            ret = os8104_reg_write(fd, OS8104_REG_BPCTC, &pctc, 1);
            if (ret) {
                fprintf(stderr, "failed to clear bPCTS: %s\n", strerror(-ret));
                goto out;
            }
            printf("Cleared packet status with bPCTC=0x%02x\n", pctc);
        }
    } else {
        printf("Timed out waiting for async packet status.\n");
    }

    ret = 0;
out:
    if (fd >= 0)
        close(fd);
    return ret ? 1 : 0;
}
