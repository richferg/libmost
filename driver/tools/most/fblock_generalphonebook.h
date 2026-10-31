#ifndef FBLOCK_GENERALPHONEBOOK_H
#define FBLOCK_GENERALPHONEBOOK_H

// GeneralPhoneBook FBlockID
#define FBLOCKID_GENERALPHONEBOOK 0x51

// GeneralPhoneBook Function IDs (generated from XML)
typedef enum {
    GENERALPHONEBOOK_FKT_INFORMATION                      = 0x204,  // Information
    GENERALPHONEBOOK_FKT_MEMORYUSAGE                      = 0x205,  // MemoryUsage
    GENERALPHONEBOOK_FKT_STANDARDPHONEBOOK                = 0x400,  // StandardPhonebook
    GENERALPHONEBOOK_FKT_CDMAPHONEBOOK                    = 0x500,  // CDMAPhonebook
    GENERALPHONEBOOK_FKT_PDCPHONEBOOK                     = 0x600,  // PDCPhonebook
} GeneralPhoneBook_FktID;

#endif // FBLOCK_GENERALPHONEBOOK_H
