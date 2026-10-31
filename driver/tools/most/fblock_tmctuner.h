#ifndef FBLOCK_TMCTUNER_H
#define FBLOCK_TMCTUNER_H

// TMCTuner FBlockID
#define FBLOCKID_TMCTUNER 0x41

// TMCTuner Function IDs (generated from XML)
typedef enum {
    TMCTUNER_FKT_FKTIDS                           = 0x000,  // FktIDs
    TMCTUNER_FKT_NOTIFICATION                     = 0x001,  // Notification
    TMCTUNER_FKT_NOTIFICATIONCHECK                = 0x002,  // NotificationCheck
    TMCTUNER_FKT_TMCPROGRAMMS                     = 0x200,  // TMCProgramms
    TMCTUNER_FKT_TMCDATASTREAM                    = 0x201,  // TMCDataStream
    TMCTUNER_FKT_TMCSWITCH                        = 0x202,  // TMCSwitch
    TMCTUNER_FKT_TMCSELECTIONFILTER               = 0x203,  // TMCSelectionFilter
    TMCTUNER_FKT_TMCSTATIONS                      = 0x204,  // TMCStations
    TMCTUNER_FKT_TMCDATA                          = 0x205,  // TMCData
    TMCTUNER_FKT_TMCSTATUS                        = 0x206,  // TMCStatus
} TMCTuner_FktID;

#endif // FBLOCK_TMCTUNER_H
