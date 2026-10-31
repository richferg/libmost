#include "catalog.h"

#include "cmt.h"

/*
 * Field layouts. Names tagged "inferred" below are best-guess from the telegram
 * name; lon/lat/speed/odometer are confirmed in the research notes.
 */

/* KombiMiscFkts 0xCA odometer: single big-endian u32, units of km. */
static const struct mostd_field f_kombi_odometer[] = {
	{ "odometer_km", MFLD_U32BE },
};

/* FbNavigation 0x52 PositionHeadingSpeed (0x020B), add* (after presence word). */
static const struct mostd_field f_nav_poshdgspd[] = {
	{ "heading",  MFLD_U16BE },	/* inferred */
	{ "lon",      MFLD_I32BE },
	{ "lat",      MFLD_I32BE },
	{ "quality",  MFLD_U8 },	/* inferred */
	{ "speed",    MFLD_U16BE },
};

/*
 * FbNavigation 0x52 Navigation_Status (0x0202). From the FbNavigation decompile
 * (Navigation_StatusStatus @ 0x037214c4): Data = [status u16][blob], with
 * DataLen = blobLen + 2. The blob is a POS-typed struct built by 054_Navigation;
 * its interior is not in the available decompiles, so it stays raw hex.
 */
static const struct mostd_field f_nav_status[] = {
	{ "status", MFLD_U16BE },
	{ "blob",   MFLD_BLOB },
};

/*
 * Live broadcasters seen on the car ring with no documented layout yet
 * (Vehicle 0x05 is a client proxy in the research, so the bus map has no rows).
 * Label + blob them so the constant stream is named and ready for correlation.
 */
static const struct mostd_field f_blob_only[] = {
	{ "data", MFLD_BLOB },
};

/* FbNavigation 0x52 PointOfNextDestination (0x020C), add*. */
static const struct mostd_field f_nav_nextdest[] = {
	{ "lon", MFLD_I32BE },
	{ "lat", MFLD_I32BE },
};

/* FbNavigation 0x52 DistanceTimeTo{Next,Final}Dest (0x0208/0x0207), add*. */
static const struct mostd_field f_nav_disttime[] = {
	{ "w0", MFLD_U16BE },	/* distance/time fields; semantics unconfirmed */
	{ "w1", MFLD_U16BE },
	{ "w2", MFLD_U16BE },
};

#define FIELDS(a) (a), (sizeof(a) / sizeof((a)[0]))

static const struct mostd_telegram_desc catalog[] = {
	{ 0xCA, 0x247, MOST_OP_STATUS, "KombiMisc.Odometer",          0, FIELDS(f_kombi_odometer) },
	{ 0x52, 0x202, MOST_OP_STATUS, "Nav.Navigation_Status",       0, FIELDS(f_nav_status) },
	{ 0x52, 0x20B, MOST_OP_STATUS, "Nav.PositionHeadingSpeed",    1, FIELDS(f_nav_poshdgspd) },
	{ 0x52, 0x20C, MOST_OP_STATUS, "Nav.PointOfNextDestination",  1, FIELDS(f_nav_nextdest) },
	{ 0x52, 0x207, MOST_OP_STATUS, "Nav.DistanceTimeToFinalDest", 1, FIELDS(f_nav_disttime) },
	{ 0x52, 0x208, MOST_OP_STATUS, "Nav.DistanceTimeToNextDest",  1, FIELDS(f_nav_disttime) },
	{ 0x05, 0x220, MOST_OP_STATUS, "Vehicle.0x220",               0, FIELDS(f_blob_only) },
	{ 0xC9, 0x308, MOST_OP_STATUS, "Vendor.0xC9.0x308",           0, FIELDS(f_blob_only) },
	/* Telephone 0x50 is a client proxy in the research (no field layouts);
	 * label + blob its Status until correlated. */
	{ 0x50, 0x427, MOST_OP_STATUS, "Tel.RegisterState",           0, FIELDS(f_blob_only) },
	{ 0x50, 0x252, MOST_OP_STATUS, "Tel.CurrentNumber",           0, FIELDS(f_blob_only) },
	{ 0x50, 0x421, MOST_OP_STATUS, "Tel.NetworkOperators",        0, FIELDS(f_blob_only) },
};

size_t mostd_field_size(enum mostd_field_type type)
{
	switch (type) {
	case MFLD_U8:
		return 1;
	case MFLD_U16BE:
		return 2;
	case MFLD_U32BE:
	case MFLD_I32BE:
		return 4;
	case MFLD_BLOB:
		return 0;	/* variable length, handled by the decoder */
	}
	return 0;
}

const struct mostd_telegram_desc *
mostd_catalog_lookup(uint8_t fblock, uint16_t fktid, uint8_t optype)
{
	size_t i;

	for (i = 0; i < sizeof(catalog) / sizeof(catalog[0]); i++) {
		const struct mostd_telegram_desc *d = &catalog[i];

		if (d->fblock == fblock && d->fktid == fktid && d->optype == optype)
			return d;
	}
	return NULL;
}
