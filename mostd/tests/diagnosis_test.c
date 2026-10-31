#include <assert.h>
#include <stdint.h>
#include <string.h>

int mostd_log_level;

#include "../protocol/diagnosis.c"

struct captured_tx {
	uint16_t device;
	uint8_t fblock;
	uint8_t inst;
	uint16_t fktid;
	uint8_t op;
	uint8_t payload[16];
	size_t length;
};

static struct captured_tx captured[8];
static size_t captured_count;

static int capture(uint16_t device, uint8_t fblock, uint8_t inst,
		   uint16_t fktid, uint8_t op,
		   const uint8_t *payload, size_t length)
{
	struct captured_tx *tx;

	assert(captured_count < sizeof(captured) / sizeof(captured[0]));
	assert(length <= sizeof(captured[0].payload));
	tx = &captured[captured_count++];
	tx->device = device;
	tx->fblock = fblock;
	tx->inst = inst;
	tx->fktid = fktid;
	tx->op = op;
	tx->length = length;
	if (length)
		memcpy(tx->payload, payload, length);
	return 0;
}

int mostd_tx_status(struct most_handle *handle, uint16_t device, uint8_t fblock,
		    uint8_t inst, uint16_t fktid, uint8_t telid,
		    const uint8_t *payload, size_t length)
{
	(void)handle;
	(void)telid;
	return capture(device, fblock, inst, fktid, MOST_OP_STATUS, payload, length);
}

int mostd_tx_op(struct most_handle *handle, uint16_t device, uint8_t fblock,
		uint8_t inst, uint16_t fktid, uint8_t op, uint8_t telid,
		const uint8_t *payload, size_t length)
{
	(void)handle;
	(void)telid;
	return capture(device, fblock, inst, fktid, op, payload, length);
}

static struct mostd_diagnosis new_server(void)
{
	struct mostd_diagnosis diag;

	mostd_diagnosis_init(&diag, (struct most_handle *)(uintptr_t)1);
	return diag;
}

static struct mostd_rx_msg new_request(uint16_t fktid, uint8_t op)
{
	struct mostd_rx_msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.source = 0x0108;
	msg.fblock = MOSTD_DIAGNOSIS_FBLOCK;
	msg.inst = MOSTD_DIAGNOSIS_INST;
	msg.fktid = fktid;
	msg.optype = op;
	return msg;
}

static void test_fktids(void)
{
	static const uint8_t expected[] = { 0x00, 0x10, 0x50, 0x05, 0x10 };
	struct mostd_diagnosis diag = new_server();
	struct mostd_rx_msg msg = new_request(FKT_FKTIDS, MOST_OP_GET);

	captured_count = 0;
	assert(mostd_diagnosis_handle_msg(&diag, &msg) == 1);
	assert(captured_count == 1);
	assert(captured[0].fblock == 0x06);
	assert(captured[0].inst == 0x54);
	assert(captured[0].fktid == 0x000);
	assert(captured[0].op == MOST_OP_STATUS);
	assert(captured[0].length == sizeof(expected));
	assert(memcmp(captured[0].payload, expected, sizeof(expected)) == 0);
}

static void test_valid_set_is_deferred(void)
{
	struct mostd_diagnosis diag = new_server();
	struct mostd_rx_msg msg = new_request(FKT_DIAGNOSTICS_CHANNEL, MOST_OP_SET);

	msg.tellen = 3;
	msg.data[0] = 0x01;
	msg.data[1] = 0x01;
	msg.data[2] = 0x02;
	captured_count = 0;
	assert(mostd_diagnosis_handle_msg(&diag, &msg) == 1);
	assert(captured_count == 0);
	assert(diag.stats.deferred == 1);
}

/* EDIABAS IDENT lands as the tunneled KWP record F1 54 1A 80; classify, no reply. */
static void test_kwp_record_classified(void)
{
	struct mostd_diagnosis diag = new_server();
	struct mostd_rx_msg msg = new_request(FKT_DIAGNOSTICS_CHANNEL, MOST_OP_SET);

	msg.tellen = 4;
	msg.data[0] = 0xF1; /* tester */
	msg.data[1] = 0x54; /* routing -> SDARS diag node */
	msg.data[2] = 0x1A; /* KWP ReadEcuIdentification */
	msg.data[3] = 0x80;
	captured_count = 0;
	assert(mostd_diagnosis_handle_msg(&diag, &msg) == 1);
	assert(captured_count == 0);
	assert(diag.stats.kwp_records == 1);
	assert(diag.stats.deferred == 0);
}

static void test_job_fingerprint(void)
{
	/* (service,subid) -> exact EDIABAS job name from SAT_60.prg */
	assert(strcmp(kwp_job_name(0x1A, 0x80), "IDENT") == 0);
	assert(strcmp(kwp_job_name(0x1A, 0x89), "SERIENNUMMER_LESEN") == 0);
	assert(strcmp(kwp_job_name(0x21, 0x11), "STATUS_OVR_TEMP_COUNTER") == 0);
	/* service-only fallback */
	assert(strcmp(kwp_job_name(0x23, 0x00), "SPEICHER_LESEN") == 0);
	/* unknown -> NULL */
	assert(kwp_job_name(0xEE, 0xEE) == NULL);
}

static void test_invalid_sets_are_silent(void)
{
	struct mostd_diagnosis diag = new_server();
	struct mostd_rx_msg msg = new_request(FKT_DIAGNOSTICS_CHANNEL, MOST_OP_SET);

	captured_count = 0;
	assert(mostd_diagnosis_handle_msg(&diag, &msg) == 1);
	assert(captured_count == 0);

	msg.tellen = 3;
	msg.data[0] = 0x02;
	msg.data[2] = 0x30;
	assert(mostd_diagnosis_handle_msg(&diag, &msg) == 1);
	assert(captured_count == 0);
}

static void test_routing(void)
{
	struct mostd_diagnosis diag = new_server();
	struct mostd_rx_msg msg = new_request(FKT_DIAGNOSTICS_CHANNEL, MOST_OP_SET);

	msg.tellen = 1;
	msg.data[0] = 0x00;
	msg.inst = 0x53;
	captured_count = 0;
	assert(mostd_diagnosis_handle_msg(&diag, &msg) == 0);
	assert(captured_count == 0);

	msg.inst = 0x00;
	assert(mostd_diagnosis_handle_msg(&diag, &msg) == 1);
	assert(captured_count == 0);
}

int main(void)
{
	test_fktids();
	test_valid_set_is_deferred();
	test_kwp_record_classified();
	test_job_fingerprint();
	test_invalid_sets_are_silent();
	test_routing();
	return 0;
}
