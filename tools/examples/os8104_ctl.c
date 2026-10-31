#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#include "most.h"

static const char *link_state_str(enum most_link_state state)
{
    switch (state) {
    case MOST_LINK_DOWN: return "down";
    case MOST_LINK_ACQUIRING: return "acquiring";
    case MOST_LINK_UP: return "up";
    case MOST_LINK_DEGRADED: return "degraded";
    default: return "unknown";
    }
}

static const char *dev_suffix_from_arg(const char *arg)
{
    static const char prefix[] = "/dev/most";
    if (!arg)
        return "0";

    if (!strncmp(arg, prefix, sizeof(prefix) - 1))
        return arg + (sizeof(prefix) - 1);

    return arg;
}

int main(int argc, char **argv)
{
    const char *dev_suffix = dev_suffix_from_arg((argc > 1) ? argv[1] : NULL);
    struct most_handle *h;
    uint16_t node;
    uint8_t group;
    enum most_link_state link_state;

    h = most_open(dev_suffix);
    if (!h) {
        perror("most_open");
        return 1;
    }

    if (most_get_node_addr(h, &node) ||
        most_get_group_addr(h, &group) ||
        most_get_link_state(h, &link_state)) {
        perror("most_get_*");
        most_close(h);
        return 1;
    }

    printf("Current: node=0x%04x group=0x%02x\n", node, group);
    printf("Link   : %s (%u)\n", link_state_str(link_state), link_state);

    node = 0x1234;
    group = 0x56;
    printf("Setting: node=0x%04x group=0x%02x\n", node, group);

    if (most_set_node_addr(h, node) ||
        most_set_group_addr(h, group)) {
        perror("most_set_*");
        most_close(h);
        return 1;
    }

    if (most_get_node_addr(h, &node) ||
        most_get_group_addr(h, &group) ||
        most_get_link_state(h, &link_state)) {
        perror("most_get_*");
        most_close(h);
        return 1;
    }

    printf("Updated: node=0x%04x group=0x%02x\n", node, group);
    printf("Link   : %s (%u)\n", link_state_str(link_state), link_state);
    most_close(h);
    return 0;
}
