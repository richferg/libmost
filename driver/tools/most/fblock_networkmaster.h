#ifndef FBLOCK_NETWORKMASTER_H
#define FBLOCK_NETWORKMASTER_H

// NetworkMaster FBlockID
#define FBLOCKID_NETWORKMASTER 0x02

// NetworkMaster Function IDs (generated from XML)
typedef enum {
    NETWORKMASTER_FKT_CONFIGURATION                    = 0xA00,  // Configuration
    NETWORKMASTER_FKT_CENTRALREGISTRY                  = 0xA01,  // CentralRegistry
    NETWORKMASTER_FKT_SAVECONFIGURATION                = 0xA02,  // SaveConfiguration
    NETWORKMASTER_FKT_BOUNDARY                         = 0xA03,  // Boundary
} NetworkMaster_FktID;

#endif // FBLOCK_NETWORKMASTER_H
