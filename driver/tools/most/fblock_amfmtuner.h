#ifndef FBLOCK_AMFMTUNER_H
#define FBLOCK_AMFMTUNER_H

// AmFmTuner FBlockID
#define FBLOCKID_AMFMTUNER 0x40

// AmFmTuner Function IDs (generated from XML)
typedef enum {
    AMFMTUNER_FKT_FKTIDS                           = 0x000,  // FktIDs
    AMFMTUNER_FKT_NOTIFICATION                     = 0x001,  // Notification
    AMFMTUNER_FKT_NOTIFICATIONCHECK                = 0x002,  // NotificationCheck
    AMFMTUNER_FKT_SOURCEINFO                       = 0x100,  // SourceInfo
    AMFMTUNER_FKT_ALLOCATE                         = 0x101,  // Allocate
    AMFMTUNER_FKT_DEALLOCATE                       = 0x102,  // DeAllocate
    AMFMTUNER_FKT_SOURCEACTIVITY                   = 0x103,  // SourceActivity
    AMFMTUNER_FKT_SOURCENAME                       = 0x104,  // SourceName
    AMFMTUNER_FKT_SOURCECONNECT                    = 0x105,  // SourceConnect
    AMFMTUNER_FKT_SOURCEDISCONNECT                 = 0x106,  // SourceDisConnect
    AMFMTUNER_FKT_SOURCEROUTING                    = 0x107,  // SourceRouting
    AMFMTUNER_FKT_SYNCDATAINFO                     = 0x116,  // SyncDataInfo
    AMFMTUNER_FKT_ATWAVEBAND                       = 0x200,  // ATWaveband
    AMFMTUNER_FKT_ATSTATIONINFO                    = 0x201,  // ATStationInfo
    AMFMTUNER_FKT_ATSEEK                           = 0x204,  // ATSeek
    AMFMTUNER_FKT_ATPI                             = 0x205,  // ATPI
    AMFMTUNER_FKT_ATFREQUENCY                      = 0x206,  // ATFrequency
    AMFMTUNER_FKT_ATPRESETSAVE                     = 0x400,  // ATPresetSave
    AMFMTUNER_FKT_ATPRESETLIST1                    = 0x401,  // ATPresetList1
    AMFMTUNER_FKT_ATPRESETLIST2                    = 0x402,  // ATPresetList2
    AMFMTUNER_FKT_ATPRESETSHORTLIST1               = 0x403,  // ATPresetShortList1
    AMFMTUNER_FKT_ATPRESETSHORTLIST2               = 0x404,  // ATPresetShortList2
    AMFMTUNER_FKT_ATPRESETSHORTLIST3               = 0x405,  // ATPresetShortList3
    AMFMTUNER_FKT_ATPRESETSHORTLIST4               = 0x406,  // ATPresetShortList4
    AMFMTUNER_FKT_ATPRESETSHORTLIST5               = 0x407,  // ATPresetShortList5
    AMFMTUNER_FKT_ATPRESETSHORTLIST6               = 0x408,  // ATPresetShortList6
    AMFMTUNER_FKT_ATPRESETSHORTLIST7               = 0x409,  // ATPresetShortList7
    AMFMTUNER_FKT_ATPRESETSHORTLIST8               = 0x40A,  // ATPresetShortList8
    AMFMTUNER_FKT_DTSTATIONS                       = 0x410,  // DTStations
    AMFMTUNER_FKT_DTTPSTATIONS                     = 0x412,  // DTTPStations
    AMFMTUNER_FKT_TPSWITCH                         = 0x450,  // TPSwitch
    AMFMTUNER_FKT_RDSSWITCH                        = 0x451,  // RDSSwitch
    AMFMTUNER_FKT_REG                              = 0x452,  // REG
    AMFMTUNER_FKT_TAESCAPE                         = 0x453,  // TAEscape
    AMFMTUNER_FKT_AUTOSTORE                        = 0x454,  // Autostore
    AMFMTUNER_FKT_TAINFO                           = 0x455,  // TAInfo
    AMFMTUNER_FKT_TAMESSAGE                        = 0x456,  // TAMessage
    AMFMTUNER_FKT_PTY31MESSAGE                     = 0x459,  // PTY31Message
    AMFMTUNER_FKT_RADIOTEXT                        = 0x45A,  // Radiotext
    AMFMTUNER_FKT_ANNOUNCEMENTESCAPE               = 0x45B,  // AnnouncementEscape
    AMFMTUNER_FKT_JPTRAF                           = 0x45C,  // JpTraf
} AmFmTuner_FktID;

#endif // FBLOCK_AMFMTUNER_H
