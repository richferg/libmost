#ifndef MOSTD_PROTOCOL_DIAGNOSIS_H
#define MOSTD_PROTOCOL_DIAGNOSIS_H

#include <stdint.h>

#include "most.h"
#include "rx.h"

#define MOSTD_DIAGNOSIS_FBLOCK	0x06u
#define MOSTD_DIAGNOSIS_INST	0x54u

struct mostd_diagnosis_stats {
	uint32_t handled;	/* 0x06.54 requests we owned */
	uint32_t replies;	/* Status telegrams we emitted */
	uint32_t errors;	/* Error telegrams we emitted */
	uint32_t deferred;	/* valid 0x050.Set immediate-leaf forms (mode 0/1) */
	uint32_t kwp_records;	/* tunneled KWP transport records (routing 0x54) seen */
};

struct mostd_diagnosis {
	struct most_handle *handle;
	struct mostd_diagnosis_stats stats;
};

void mostd_diagnosis_init(struct mostd_diagnosis *diag, struct most_handle *handle);

/* Handle the firmware-backed Diagnosis 0x06.54 surface. 1=consumed, 0=not ours. */
int mostd_diagnosis_handle_msg(struct mostd_diagnosis *diag,
			       const struct mostd_rx_msg *msg);

/* Proactively emit the 0x050 DiagnosticsChannel STATUS to dst (SAT-select replay). */
int mostd_diagnosis_emit_channel_status(struct mostd_diagnosis *diag, uint16_t dst);

#endif /* MOSTD_PROTOCOL_DIAGNOSIS_H */
