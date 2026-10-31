#include "decode.h"

#include <stdarg.h>
#include <stdio.h>

#include "catalog.h"

static uint16_t rd_u16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd_u32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Append to a bounded cursor; never overflows. */
static void appendf(char **cur, char *end, const char *fmt, ...)
{
	int n;
	va_list ap;

	if (*cur >= end)
		return;
	va_start(ap, fmt);
	n = vsnprintf(*cur, (size_t)(end - *cur), fmt, ap);
	va_end(ap);
	if (n > 0)
		*cur += (n < end - *cur) ? n : (end - *cur);
}

int mostd_decode_telegram(const struct mostd_rx_msg *msg, char *out, size_t outsz)
{
	const struct mostd_telegram_desc *d;
	size_t fixed;
	size_t off;
	size_t i;
	int has_blob = 0;
	int bad;
	char *cur = out;
	char *end = out + outsz;

	if (!msg || !out || outsz == 0)
		return 0;
	out[0] = '\0';

	d = mostd_catalog_lookup(msg->fblock, msg->fktid, msg->optype);
	if (!d)
		return 0;

	/* fixed = the non-blob part; a trailing BLOB takes whatever remains. */
	fixed = (d->has_presence ? 2u : 0u);
	for (i = 0; i < d->field_count; i++) {
		if (d->fields[i].type == MFLD_BLOB)
			has_blob = 1;
		else
			fixed += mostd_field_size(d->fields[i].type);
	}

	bad = has_blob ? (msg->tellen < fixed) : (msg->tellen != fixed);
	if (bad) {
		if (d->has_presence && msg->tellen >= 2)
			appendf(&cur, end, "%s presence=0x%04X ", d->name, rd_u16(msg->data));
		else
			appendf(&cur, end, "%s ", d->name);
		appendf(&cur, end, "len=%u %s %zu (raw)", msg->tellen,
			has_blob ? "min" : "expected", fixed);
		return -1;
	}

	appendf(&cur, end, "%s:", d->name);
	off = 0;
	if (d->has_presence) {
		appendf(&cur, end, " presence=0x%04X", rd_u16(msg->data));
		off = 2;
	}

	for (i = 0; i < d->field_count; i++) {
		const struct mostd_field *f = &d->fields[i];
		const uint8_t *p = msg->data + off;

		switch (f->type) {
		case MFLD_U8:
			appendf(&cur, end, " %s=%u", f->name, p[0]);
			break;
		case MFLD_U16BE:
			appendf(&cur, end, " %s=%u", f->name, rd_u16(p));
			break;
		case MFLD_U32BE:
			appendf(&cur, end, " %s=%u", f->name, rd_u32(p));
			break;
		case MFLD_I32BE:
			appendf(&cur, end, " %s=%d", f->name, (int32_t)rd_u32(p));
			break;
		case MFLD_BLOB: {
			size_t j;

			appendf(&cur, end, " %s=", f->name);
			for (j = off; j < msg->tellen; j++)
				appendf(&cur, end, "%02X", msg->data[j]);
			off = msg->tellen;
			break;
		}
		}
		off += mostd_field_size(f->type);
	}

	return 1;
}
