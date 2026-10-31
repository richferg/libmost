/*
 * Auto-generated from diag/e65prg/SAT_60.prg KWP request fingerprints.
 * Key = (service<<8)|subid for exact, service for fallback. First name used;
 * collisions noted in comments. Regenerate from the SGBD, do not hand-edit.
 *
 * Lets the Diagnosis 0x06 logger name the exact EDIABAS job behind a tunneled
 * KWP record (offsets 2,3 = service,subid) even though mostd does not reply.
 */
#ifndef MOSTD_PROTOCOL_DIAGNOSIS_JOBMAP_H
#define MOSTD_PROTOCOL_DIAGNOSIS_JOBMAP_H

#include <stdint.h>
#include <stddef.h>

struct kwp_job { uint16_t key; const char *name; };

static const struct kwp_job kwp_jobs_exact[] = {
	{ 0x1101, "STEUERGERAETE_RESET" },
	{ 0x1A18, "PRUEFCODE_LESEN" },
	{ 0x1A23, "TEST_TONE_START" }, /* +STEUERN_TESTTON_START_SDARS */
	{ 0x1A80, "IDENT" },
	{ 0x1A87, "PHYSIKALISCHE_HW_NR_LESEN" },
	{ 0x1A89, "SERIENNUMMER_LESEN" }, /* +STATUS_DEVICE_ID_LESEN */
	{ 0x1A90, "C_FG_LESEN" },
	{ 0x1A9B, "C_CI_LESEN" },
	{ 0x2111, "STATUS_OVR_TEMP_COUNTER" },
	{ 0x21A0, "MOST_VERSION_LESEN" },
	{ 0x21AD, "STATUS_WAKE_UP_STATUS" }, /* +STATUS_ABILITY_TO_WAKE */
	{ 0x21AF, "STATUS_MOST_3DB" },
	{ 0x2210, "PRUEFSTEMPEL_LESEN" },
	{ 0x2220, "IS_LESEN" }, /* +IS_LESEN_DETAIL */
	{ 0x2225, "ZIF_LESEN" }, /* +ZIF_BACKUP_LESEN,HARDWARE_REFERENZ_LESEN,DATEN_REFERENZ_LESEN,FLASH_ZEITEN_LESEN,FLASH_BLOCKLAENGE_LESEN */
	{ 0x2230, "C_C_LESEN" },
	{ 0x223F, "C_AEI_LESEN" },
	{ 0x2829, "NORMALER_DATENVERKEHR" },
	{ 0x2E10, "PRUEFSTEMPEL_SCHREIBEN" },
	{ 0x2E30, "C_C_SCHREIBEN" }, /* +C_C_AUFTRAG */
	{ 0x2E3F, "C_AEI_SCHREIBEN" }, /* +C_AEI_AUFTRAG */
	{ 0x3102, "FLASH_LOESCHEN" },
	{ 0x3106, "IS_LOESCHEN" },
	{ 0x3107, "AUTHENTISIERUNG_ZUFALLSZAHL_LESEN" },
	{ 0x3108, "AUTHENTISIERUNG_START" },
	{ 0x3109, "FLASH_SIGNATUR_PRUEFEN" },
	{ 0x310A, "FLASH_PROGRAMMIER_STATUS_LESEN" },
	{ 0x310E, "SLEEP_MODE" },
	{ 0x3B90, "C_FG_SCHREIBEN" }, /* +C_FG_AUFTRAG */
	{ 0x3BAD, "STEUERN_ABILITY_TO_WAKE" },
	{ 0x3BAF, "STEUERN_MOST_3DB" },
};

static const struct kwp_job kwp_jobs_svc[] = {
	{ 0x10, "DIAGNOSE_MODE" },
	{ 0x14, "FS_LOESCHEN" },
	{ 0x17, "FS_LESEN_DETAIL" },
	{ 0x18, "FS_LESEN" },
	{ 0x20, "DIAGNOSE_ENDE" },
	{ 0x23, "SPEICHER_LESEN" }, /* +AIF_LESEN */
	{ 0x34, "FLASH_SCHREIBEN_ADRESSE" },
	{ 0x36, "FLASH_SCHREIBEN" },
	{ 0x37, "FLASH_SCHREIBEN_ENDE" },
	{ 0x3D, "SPEICHER_SCHREIBEN" }, /* +AIF_SCHREIBEN */
	{ 0x3E, "DIAGNOSE_AUFRECHT" },
};

/* Best-effort job name for a tunneled KWP record; NULL if unknown. */
static inline const char *kwp_job_name(uint8_t service, uint8_t subid)
{
	uint16_t key = (uint16_t)((service << 8) | subid);
	size_t i;

	for (i = 0; i < sizeof(kwp_jobs_exact) / sizeof(kwp_jobs_exact[0]); i++)
		if (kwp_jobs_exact[i].key == key)
			return kwp_jobs_exact[i].name;
	for (i = 0; i < sizeof(kwp_jobs_svc) / sizeof(kwp_jobs_svc[0]); i++)
		if (kwp_jobs_svc[i].key == service)
			return kwp_jobs_svc[i].name;
	return NULL;
}

#endif /* MOSTD_PROTOCOL_DIAGNOSIS_JOBMAP_H */
