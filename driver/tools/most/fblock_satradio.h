#ifndef FBLOCK_SATRADIO_H
#define FBLOCK_SATRADIO_H

// SatRadio FBlockID
#define FBLOCKID_SATRADIO 0x44

// SatRadio Function IDs (generated from XML)
typedef enum {
    SATRADIO_FKT_FKTIDS                           = 0x000,  // FktIDs
    SATRADIO_FKT_NOTIFICATION                     = 0x001,  // Notification
    SATRADIO_FKT_NOTIFICATIONCHECK                = 0x002,  // NotificationCheck
    SATRADIO_FKT_CREATEARRAYWINDOW                = 0x090,  // CreateArrayWindow
    SATRADIO_FKT_DESTROYARRAYWINDOW               = 0x091,  // DestroyArrayWindow
    SATRADIO_FKT_MOVEARRAYWINDOW                  = 0x092,  // MoveArrayWindow
    SATRADIO_FKT_SEARCHARRAYWINDOW                = 0x093,  // SearchArrayWindow
    SATRADIO_FKT_LONGARRAYINFO                    = 0x094,  // LongArrayInfo
    SATRADIO_FKT_SOURCEINFO                       = 0x100,  // SourceInfo
    SATRADIO_FKT_ALLOCATE                         = 0x101,  // Allocate
    SATRADIO_FKT_DEALLOCATE                       = 0x102,  // DeAllocate
    SATRADIO_FKT_SOURCEACTIVITY                   = 0x103,  // SourceActivity
    SATRADIO_FKT_SOURCENAME                       = 0x104,  // SourceName
    SATRADIO_FKT_SOURCECONNECT                    = 0x105,  // SourceConnect
    SATRADIO_FKT_SOURCEDISCONNECT                 = 0x106,  // SourceDisConnect
    SATRADIO_FKT_SOURCEROUTING                    = 0x107,  // SourceRouting
    SATRADIO_FKT_SYNCDATAINFO                     = 0x116,  // SyncDataInfo
    SATRADIO_FKT_STATIONSELECT                    = 0x210,  // StationSelect
    SATRADIO_FKT_STATIONTUNE                      = 0x211,  // StationTune
    SATRADIO_FKT_CURRENTSTATIONINFO               = 0x212,  // CurrentStationInfo
    SATRADIO_FKT_CURRENTCATEGORYINFO              = 0x213,  // CurrentCategoryInfo
    SATRADIO_FKT_RADIOTEXT                        = 0x214,  // RadioText
    SATRADIO_FKT_SERVICEPROVIDER                  = 0x215,  // ServiceProvider
    SATRADIO_FKT_ELECTRONICCODE                   = 0x216,  // ElectronicCode
    SATRADIO_FKT_SERVICESTATUS                    = 0x217,  // ServiceStatus
    SATRADIO_FKT_PRESETS                          = 0x410,  // Presets
    SATRADIO_FKT_TUNINGSTATUS                     = 0x411,  // TuningStatus
    SATRADIO_FKT_STATIONLISTINFO                  = 0x412,  // StationListInfo
    SATRADIO_FKT_CATEGORYSELECT                   = 0x413,  // CategorySelect
    SATRADIO_FKT_CATEGORYLISTINFO                 = 0x414,  // CategoryListInfo
    SATRADIO_FKT_STATIONLISTFORCATEGORY           = 0x415,  // StationListForCategory
} SatRadio_FktID;

#endif // FBLOCK_SATRADIO_H
