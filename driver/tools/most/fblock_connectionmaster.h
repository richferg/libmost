#ifndef FBLOCK_CONNECTIONMASTER_H
#define FBLOCK_CONNECTIONMASTER_H

// ConnectionMaster FBlockID
#define FBLOCKID_CONNECTIONMASTER 0x03

// ConnectionMaster Function IDs (generated from XML)
typedef enum {
    CONNECTIONMASTER_FKT_BUILDSYNCCONNECTION              = 0x200,  // BuildSyncConnection
    CONNECTIONMASTER_FKT_REMOVESYNCCONNECTION             = 0x201,  // RemoveSyncConnection
    CONNECTIONMASTER_FKT_SYNCCONNECTIONTABLE              = 0x400,  // SyncConnectionTable
    CONNECTIONMASTER_FKT_AVAILABLECHANNELS                = 0x401,  // AvailableChannels
    CONNECTIONMASTER_FKT_MOVEBOUNDARY                     = 0x402,  // MoveBoundary
    CONNECTIONMASTER_FKT_BOUNDARYCHANGE                   = 0x403,  // BoundaryChange
} ConnectionMaster_FktID;

#endif // FBLOCK_CONNECTIONMASTER_H
