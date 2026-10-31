#include <assert.h>
#include <stdint.h>
#include <string.h>

int mostd_log_level;

#include "../protocol/netblock_server.c"

static struct {
	uint16_t device;
	uint8_t fblock;
	uint8_t inst;
	uint16_t fktid;
	uint8_t payload[16];
	size_t length;
} captured;

int mostd_tx_status(struct most_handle *handle, uint16_t device, uint8_t fblock,
		    uint8_t inst, uint16_t fktid, uint8_t telid,
		    const uint8_t *payload, size_t length)
{
	(void)handle;
	(void)telid;
	assert(length <= sizeof(captured.payload));
	captured.device = device;
	captured.fblock = fblock;
	captured.inst = inst;
	captured.fktid = fktid;
	captured.length = length;
	memcpy(captured.payload, payload, length);
	return 0;
}

int mostd_tx_error_for_request(struct most_handle *handle, uint16_t device,
			       uint8_t fblock, uint8_t inst, uint16_t fktid,
			       uint8_t telid, uint8_t request_op,
			       const uint8_t *request_payload, size_t request_length,
			       uint8_t error_code, uint8_t error_info)
{
	(void)handle;
	(void)device;
	(void)fblock;
	(void)inst;
	(void)fktid;
	(void)telid;
	(void)request_op;
	(void)request_payload;
	(void)request_length;
	(void)error_code;
	(void)error_info;
	assert(!"unexpected NetBlock error");
	return -1;
}

int most_reg_read(struct most_handle *handle, uint16_t reg, void *buf, uint16_t len)
{
	(void)handle;
	(void)reg;
	assert(len == 1);
	*(uint8_t *)buf = 0x0D;
	return 0;
}

int most_get_node_addr(struct most_handle *handle, uint16_t *addr)
{
	(void)handle;
	*addr = 0x0110;
	return 0;
}

int most_set_node_addr(struct most_handle *handle, uint16_t addr)
{
	(void)handle;
	(void)addr;
	return 0;
}

int most_get_group_addr(struct most_handle *handle, uint8_t *group)
{
	(void)handle;
	*group = 0x44;
	return 0;
}

int most_set_group_addr(struct most_handle *handle, uint8_t group)
{
	(void)handle;
	(void)group;
	return 0;
}

int main(void)
{
	static const uint8_t expected[] = { 0x44, 0x00, 0x06, 0x54 };
	struct mostd_netblock_server nb;
	struct mostd_rx_msg msg;

	mostd_netblock_server_init(&nb, (struct most_handle *)(uintptr_t)1);
	memset(&msg, 0, sizeof(msg));
	msg.source = 0x0100;
	msg.fblock = 0x01;
	msg.inst = 0x00;
	msg.fktid = 0x000;
	msg.optype = MOST_OP_GET;

	memset(&captured, 0, sizeof(captured));
	assert(mostd_netblock_server_handle_msg(&nb, &msg) == 1);
	assert(nb.advertised == 1);
	assert(captured.device == msg.source);
	assert(captured.fblock == 0x01);
	assert(captured.inst == 0x0D);
	assert(captured.fktid == 0x000);
	assert(captured.length == sizeof(expected));
	assert(memcmp(captured.payload, expected, sizeof(expected)) == 0);
	return 0;
}
